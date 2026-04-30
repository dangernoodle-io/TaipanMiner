<script lang="ts">
  import { onMount } from 'svelte'
  import { stats, info, pool } from '../lib/stores'
  import { fetchPool, putPool, switchPool, type PoolConfigInput, type PoolPutBody } from '../lib/api'
  import { fmtRelative } from '../lib/fmt'

  type PoolForm = { host: string; port: number; wallet: string; worker: string; pool_pass: string }

  // nbits is a 4-byte compact representation of the target. Difficulty 1
  // corresponds to target 0x00000000FFFF0000…, i.e. nbits 0x1d00ffff. Network
  // difficulty = difficulty1_target / target.
  function nbitsToDifficulty(nbits: string): number {
    const word = parseInt(nbits, 16)
    if (!Number.isFinite(word) || word === 0) return 0
    const exp = word >>> 24
    const mantissa = word & 0x007fffff
    if (mantissa === 0) return 0
    const targetExp = (exp - 3) * 8
    // difficulty = (0xffff << ((0x1d - 3) * 8)) / (mantissa << ((exp - 3) * 8))
    //            = (0xffff / mantissa) * 2 ** ((0x1d - exp) * 8)
    return (0xffff / mantissa) * Math.pow(2, (0x1d - exp) * 8)
  }

  function fmtNetDiff(d: number): string {
    if (!Number.isFinite(d) || d <= 0) return '—'
    if (d >= 1e12) return (d / 1e12).toFixed(2) + 'T'
    if (d >= 1e9)  return (d / 1e9).toFixed(2) + 'G'
    if (d >= 1e6)  return (d / 1e6).toFixed(2) + 'M'
    if (d >= 1e3)  return (d / 1e3).toFixed(2) + 'k'
    return d.toFixed(0)
  }

  // Extract printable scriptSig tag after the BIP34 height push. Pools usually
  // embed their brand here (e.g. "Hashed Max-DGB-Pool", "/ViaBTC/").
  function coinbaseTag(coinb1: string, coinb2: string): string | null {
    if (!coinb1) return null
    let off = 41
    const sigLen = parseInt(coinb1.slice(off * 2, off * 2 + 2), 16)
    if (!Number.isFinite(sigLen) || sigLen >= 0xfd) return null
    off += 1
    const pushLen = parseInt(coinb1.slice(off * 2, off * 2 + 2), 16)
    if (!Number.isFinite(pushLen) || pushLen < 1 || pushLen > 8) return null
    off += 1 + pushLen
    const tail = coinb1.slice(off * 2) + (coinb2 ?? '')
    let txt = ''
    for (let i = 0; i + 1 < tail.length; i += 2) {
      const b = parseInt(tail.slice(i, i + 2), 16)
      if (b >= 0x20 && b <= 0x7e) txt += String.fromCharCode(b)
      else if (txt.length >= 4) break
      else txt = ''
    }
    txt = txt.trim().replace(/^[\/\-\s]+|[\/\-\s]+$/g, '')
    return txt.length >= 3 ? txt : null
  }

  // BIP34: block height is push-encoded at the start of the coinbase scriptSig.
  // coinb1 layout: version(4) + in_count(1) + prev(32) + prev_idx(4) + scriptSig_len(varint) + scriptSig…
  function coinbaseHeight(coinb1: string): number | null {
    if (!coinb1 || coinb1.length < 84) return null
    let off = 41 // bytes of fixed prefix; scriptSig length varint follows
    const lenByte = parseInt(coinb1.slice(off * 2, off * 2 + 2), 16)
    if (!Number.isFinite(lenByte)) return null
    off += 1
    if (lenByte >= 0xfd) return null // longer varints unused for coinbase scriptSig
    const pushLen = parseInt(coinb1.slice(off * 2, off * 2 + 2), 16)
    if (!Number.isFinite(pushLen) || pushLen < 1 || pushLen > 8) return null
    off += 1
    const bytes: number[] = []
    for (let i = 0; i < pushLen; i++) {
      const b = parseInt(coinb1.slice((off + i) * 2, (off + i) * 2 + 2), 16)
      if (!Number.isFinite(b)) return null
      bytes.push(b)
    }
    let h = 0
    for (let i = bytes.length - 1; i >= 0; i--) h = h * 256 + bytes[i]
    return h
  }


  let saving = false
  let saveMsg = ''
  let rebootRequired = false
  let editingIdx: number | null = null  // 0 = primary, 1 = fallback
  let switching = false

  let form: PoolForm = { host: '', port: 0, wallet: '', worker: '', pool_pass: '' }
  let autoRotate = false
  let hostname = ''

  onMount(() => {
    // No-op; pool store auto-polls via stores.ts
  })

  function startEdit(idx: number) {
    editingIdx = idx
    saveMsg = ''
    const cfg = idx === 0 ? $pool?.configured?.primary : $pool?.configured?.fallback
    if (cfg) {
      form = {
        host: cfg.host,
        port: cfg.port,
        wallet: cfg.wallet,
        worker: cfg.worker,
        pool_pass: ''
      }
    } else {
      form = { host: '', port: 0, wallet: '', worker: '', pool_pass: '' }
    }
  }

  function cancelEdit() {
    editingIdx = null
    saveMsg = ''
  }

  async function handleSave() {
    if (editingIdx === null) return
    saveMsg = ''
    saving = true
    try {
      const body: PoolPutBody = {
        primary: $pool?.configured?.primary ? {
          host: editingIdx === 0 ? form.host.trim() : $pool.configured?.primary.host,
          port: editingIdx === 0 ? form.port : $pool.configured?.primary.port,
          worker: editingIdx === 0 ? form.worker.trim() : $pool.configured?.primary.worker,
          wallet: editingIdx === 0 ? form.wallet.trim() : $pool.configured?.primary.wallet,
          pool_pass: editingIdx === 0 ? form.pool_pass : ''
        } : {
          host: form.host.trim(),
          port: form.port,
          worker: form.worker.trim(),
          wallet: form.wallet.trim(),
          pool_pass: form.pool_pass
        },
        fallback: editingIdx === 1 ? {
          host: form.host.trim(),
          port: form.port,
          worker: form.worker.trim(),
          wallet: form.wallet.trim(),
          pool_pass: form.pool_pass
        } : ($pool?.configured?.fallback ? {
          host: $pool.configured?.fallback.host,
          port: $pool.configured?.fallback.port,
          worker: $pool.configured?.fallback.worker,
          wallet: $pool.configured?.fallback.wallet,
          pool_pass: ''
        } : null)
      }
      const res = await putPool(body)
      rebootRequired = res.reboot_required
      saveMsg = res.reboot_required ? 'Saved. Reboot required.' : 'Saved.'
      editingIdx = null
      pool.set(await fetchPool())
    } catch (e) {
      saveMsg = `Save failed: ${(e as Error).message}`
    } finally {
      saving = false
    }
  }

  async function handleSwitch(idx: 0 | 1) {
    switching = true
    try {
      await switchPool(idx)
      pool.set(await fetchPool())
    } catch (e) {
      saveMsg = `Switch failed: ${(e as Error).message}`
    } finally {
      switching = false
    }
  }

  function truncWallet(w: string | undefined): string {
    if (!w) return '—'
    if (w.length <= 14) return w
    return `${w.slice(0, 6)}…${w.slice(-4)}`
  }
</script>

<div class="pool-grid">
  <!-- Active pool status — read-only metrics from /api/pool (TA-281). -->
  <section class="card active">
    <header class="active-head">
      <h3>Active</h3>
      {#if $pool?.notify && coinbaseTag($pool.notify.coinb1, $pool.notify.coinb2)}
        <span class="pool-tag" title="coinbase scriptSig tag">{coinbaseTag($pool.notify.coinb1, $pool.notify.coinb2)}</span>
      {/if}
    </header>
    <div class="status-row">
      <div class="who">
        <div class="host">
          <span class="dot" class:connected={$pool?.connected === true}
                            class:disconnected={$pool?.connected === false}
                            class:unknown={$pool == null}
                aria-hidden="true"></span>
          {$pool?.host ?? '—'}:{$pool?.port ?? '—'}
        </div>
        <div class="sub">
          worker {$pool?.worker ?? '—'}
        </div>
      </div>
      <div class="metrics">
        <div class="m">
          <div class="v">{$pool?.current_difficulty ?? '—'}</div>
          <div class="k">diff</div>
        </div>
        <div class="m">
          <div class="v">{$pool?.session_start_ago_s != null ? fmtRelative($pool.session_start_ago_s) : '—'}</div>
          <div class="k">session</div>
        </div>
        <div class="m">
          <div class="v">{$pool?.latency_ms != null ? `${$pool.latency_ms} ms` : '—'}</div>
          <div class="k">latency</div>
        </div>
      </div>
    </div>
  </section>

  <!-- Stratum notify preview (TA-288). -->
  {#if $pool?.notify}
    {@const n = $pool.notify}
    <section class="card stratum">
      <header class="stratum-head">
        <h3>Current Job</h3>
        <span class="job-id" title="job_id">#{n.job_id}</span>
        {#if n.clean_jobs}
          <span class="clean" title="clean_jobs flag set in mining.notify">CLEAN</span>
        {/if}
      </header>
      <div class="stratum-grid">
        {#if coinbaseHeight(n.coinb1) != null}
          <div class="sf">
            <div class="sk">block height</div>
            <div class="sv mono">{fmtNetDiff(coinbaseHeight(n.coinb1) ?? 0)}</div>
          </div>
        {/if}
        <div class="sf">
          <div class="sk">prev block</div>
          <div class="sv mono" title={n.prev_hash}>
            {n.prev_hash.slice(0, 8)}…{n.prev_hash.slice(-8)}
          </div>
        </div>
        <div class="sf">
          <div class="sk">version</div>
          <div class="sv mono">0x{n.version}</div>
        </div>
        <div class="sf">
          <div class="sk">network diff</div>
          <div class="sv mono">{fmtNetDiff(nbitsToDifficulty(n.nbits))}</div>
        </div>
      </div>
    </section>
  {/if}

  <!-- Pool rows -->
  <section class="card pools">
    <header class="pools-head">
      <h3>Pools</h3>
      <label class="rotate" class:disabled-ctrl={true}>
        <input type="checkbox" bind:checked={autoRotate} disabled />
        <span>Auto rotate</span>
        <span class="pending-tag">TA-203</span>
      </label>
    </header>

    {#if $pool}
      <div class="pool-list">
        <!-- Primary -->
        <div class="pool-row" class:editing={editingIdx === 0}>
          {#if editingIdx !== 0}
            <div class="summary">
              <span class="idx">1</span>
              <span class="kind">Primary</span>
              {#if $pool.configured?.primary}
                <span class="endpoint">{$pool.configured?.primary.host}{#if $pool.configured?.primary.port}:{$pool.configured?.primary.port}{/if}</span>
                <span class="worker">{$pool.configured?.primary.worker}</span>
                <span class="wallet mono" title={$pool.configured?.primary.wallet}>{truncWallet($pool.configured?.primary.wallet)}</span>
                <span class="pass">••••</span>
                {#if $pool.active_pool_idx === 0 && $pool.connected}
                  <span class="active-tag">ACTIVE</span>
                {:else if $pool.active_pool_idx === 1 && $pool.configured?.fallback}
                  <button class="btn outline sm" on:click={() => handleSwitch(0)} disabled={switching}>{switching ? 'Switching…' : 'Switch'}</button>
                {/if}
                <button class="btn outline sm" on:click={() => startEdit(0)}>Edit</button>
              {:else}
                <span class="placeholder">not configured</span>
                <button class="btn outline sm" on:click={() => startEdit(0)}>Configure</button>
              {/if}
            </div>
          {:else}
            <form class="edit-form" on:submit|preventDefault={handleSave}>
              <div class="edit-head">
                <span class="idx">1</span>
                <span class="kind">Primary</span>
              </div>
              <div class="fields">
                <label>
                  <span class="lbl">Host</span>
                  <input type="text" bind:value={form.host} maxlength="63" required />
                </label>
                <label class="narrow">
                  <span class="lbl">Port</span>
                  <input type="number" bind:value={form.port} min="1" max="65535" required />
                </label>
                <label>
                  <span class="lbl">Worker</span>
                  <input type="text" bind:value={form.worker} placeholder={hostname || $info?.worker_name || 'miner-1'} required />
                </label>
                <label class="wide">
                  <span class="lbl">Wallet</span>
                  <input type="text" bind:value={form.wallet} spellcheck="false" required />
                </label>
                <label>
                  <span class="lbl">Password</span>
                  <input type="text" bind:value={form.pool_pass} placeholder="x" />
                </label>
              </div>
              <div class="actions">
                <button type="submit" class="btn primary sm" disabled={saving}>{saving ? 'Saving…' : 'Save'}</button>
                <button type="button" class="btn outline sm" on:click={cancelEdit} disabled={saving}>Cancel</button>
                {#if saveMsg}<span class="msg" class:warn={rebootRequired}>{saveMsg}</span>{/if}
              </div>
            </form>
          {/if}
        </div>

        <!-- Fallback -->
        <div class="pool-row" class:editing={editingIdx === 1} class:disabled={!$pool.configured?.fallback && editingIdx !== 1}>
          {#if editingIdx !== 1}
            <div class="summary">
              <span class="idx">2</span>
              <span class="kind">Fallback</span>
              {#if $pool.configured?.fallback}
                <span class="endpoint">{$pool.configured?.fallback.host}{#if $pool.configured?.fallback.port}:{$pool.configured?.fallback.port}{/if}</span>
                <span class="worker">{$pool.configured?.fallback.worker}</span>
                <span class="wallet mono" title={$pool.configured?.fallback.wallet}>{truncWallet($pool.configured?.fallback.wallet)}</span>
                <span class="pass">••••</span>
                {#if $pool.active_pool_idx === 1 && $pool.connected}
                  <span class="active-tag">ACTIVE</span>
                {:else if $pool.active_pool_idx === 0 && $pool.configured?.primary}
                  <button class="btn outline sm" on:click={() => handleSwitch(1)} disabled={switching}>{switching ? 'Switching…' : 'Switch'}</button>
                {/if}
                <button class="btn outline sm" on:click={() => startEdit(1)}>Edit</button>
              {:else}
                <span class="placeholder">not configured · optional second pool for failover</span>
                <button class="btn outline sm" on:click={() => startEdit(1)}>+ Add</button>
              {/if}
            </div>
          {:else}
            <form class="edit-form" on:submit|preventDefault={handleSave}>
              <div class="edit-head">
                <span class="idx">2</span>
                <span class="kind">Fallback</span>
              </div>
              <div class="fields">
                <label>
                  <span class="lbl">Host</span>
                  <input type="text" bind:value={form.host} maxlength="63" required />
                </label>
                <label class="narrow">
                  <span class="lbl">Port</span>
                  <input type="number" bind:value={form.port} min="1" max="65535" required />
                </label>
                <label>
                  <span class="lbl">Worker</span>
                  <input type="text" bind:value={form.worker} placeholder={hostname || $info?.worker_name || 'miner-1'} required />
                </label>
                <label class="wide">
                  <span class="lbl">Wallet</span>
                  <input type="text" bind:value={form.wallet} spellcheck="false" required />
                </label>
                <label>
                  <span class="lbl">Password</span>
                  <input type="text" bind:value={form.pool_pass} placeholder="x" />
                </label>
              </div>
              <div class="actions">
                <button type="submit" class="btn primary sm" disabled={saving}>{saving ? 'Saving…' : 'Save'}</button>
                <button type="button" class="btn outline sm" on:click={cancelEdit} disabled={saving}>Cancel</button>
                {#if saveMsg}<span class="msg" class:warn={rebootRequired}>{saveMsg}</span>{/if}
              </div>
            </form>
          {/if}
        </div>
      </div>
    {/if}
  </section>
</div>

<style>
  .pool-grid {
    display: grid;
    grid-template-columns: 1fr;
    gap: 14px;
  }

  .card.pending { opacity: 0.75; }

  .stratum-head {
    display: flex;
    align-items: center;
    gap: 10px;
    margin-bottom: 10px;
  }

  .active-head {
    display: flex;
    align-items: baseline;
    gap: 10px;
    margin-bottom: 10px;
  }


  .pool-tag {
    display: inline-block;
    font-size: 11px;
    font-weight: 600;
    color: var(--text);
    padding: 2px 8px;
    border-radius: 4px;
    background: color-mix(in srgb, var(--accent) 18%, transparent);
    border: 1px solid color-mix(in srgb, var(--accent) 45%, transparent);
    max-width: 220px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    vertical-align: middle;
    margin-left: 6px;
  }

  .stratum-head .job-id {
    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
    font-size: 12px;
    color: var(--accent);
    font-weight: 600;
  }

  .stratum-head .clean {
    font-size: 9px;
    font-weight: 700;
    letter-spacing: 0.5px;
    padding: 2px 6px;
    border-radius: 4px;
    color: var(--success);
    border: 1px solid color-mix(in srgb, var(--success) 50%, transparent);
    background: color-mix(in srgb, var(--success) 12%, transparent);
  }

  .stratum-grid {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    gap: 18px;
    flex-wrap: wrap;
  }

  .stratum-grid .sf { text-align: left; }
  .stratum-grid .sf:last-child { text-align: right; }

  @media (max-width: 720px) {
    .stratum-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
      gap: 10px 18px;
    }
    .stratum-grid .sf,
    .stratum-grid .sf:last-child { text-align: left; }
  }

  .sf .sk {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    margin-bottom: 2px;
    text-align: inherit;
  }

  .sf .sv {
    font-size: 13px;
    color: var(--text);
    font-variant-numeric: tabular-nums;
    text-align: inherit;
  }

  .sf .sv.mono {
    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  }

  h3 {
    margin: 0;
    font-size: 12px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    font-weight: 600;
  }

  .pending-tag {
    display: inline-block;
    font-size: 9px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--warning);
    background: rgba(243, 156, 18, 0.12);
    padding: 1px 6px;
    border-radius: 3px;
    font-variant-numeric: tabular-nums;
  }

  .active-tag {
    display: inline-block;
    font-size: 9px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--success);
    background: color-mix(in srgb, var(--success) 12%, transparent);
    padding: 1px 6px;
    border-radius: 3px;
    font-variant-numeric: tabular-nums;
  }

  .loading, .error { font-size: 12px; color: var(--muted); }
  .error { color: var(--danger); }

  /* Active card */
  .active h3 { margin-bottom: 12px; }

  .status-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 24px;
    flex-wrap: wrap;
  }

  .who .host {
    font-size: 18px;
    font-weight: 600;
    color: var(--text);
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .dot {
    display: inline-block;
    width: 8px;
    height: 8px;
    border-radius: 50%;
    flex-shrink: 0;
    background: var(--muted);
  }
  .dot.connected { background: var(--success); }
  .dot.disconnected { background: var(--danger); }
  .dot.unknown { background: var(--muted); }

  .who .sub {
    font-size: 11px;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    margin-top: 4px;
  }

  .metrics { display: flex; gap: 24px; }

  .m .v {
    font-size: 16px;
    font-weight: 600;
    color: var(--text);
    font-variant-numeric: tabular-nums;
    line-height: 1.1;
  }

  .m .k {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    margin-top: 2px;
  }


  /* Hostname strip */
  .hostname { padding: 10px 16px; }

  .host-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
  }

  .host-row .k {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--muted);
  }

  .host-row .v {
    font-size: 13px;
    color: var(--text);
    font-family: ui-monospace, Menlo, monospace;
  }

  /* Pools card header */
  .pools-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    margin-bottom: 14px;
  }

  .rotate {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    cursor: not-allowed;
  }

  .rotate.disabled-ctrl { opacity: 0.6; }

  /* Pool list */
  .pool-list {
    display: flex;
    flex-direction: column;
  }

  .pool-row + .pool-row {
    border-top: 1px dashed var(--border);
  }

  .pool-row.disabled { opacity: 0.5; }
  .pool-row.editing { background: var(--bg); }

  .summary {
    display: grid;
    grid-template-columns: 24px 80px 1fr 120px 140px 50px auto;
    align-items: center;
    gap: 12px;
    padding: 10px 4px;
    font-size: 12px;
  }

  @media (max-width: 720px) {
    .summary {
      grid-template-columns: 24px 80px 1fr auto;
      grid-template-areas:
        "idx kind endpoint edit"
        ".   .    worker   ."
        ".   .    wallet   ."
        ".   .    pass     .";
      row-gap: 4px;
    }
    .summary .idx { grid-area: idx; }
    .summary .kind { grid-area: kind; }
    .summary .endpoint { grid-area: endpoint; }
    .summary .worker { grid-area: worker; color: var(--muted); }
    .summary .wallet { grid-area: wallet; color: var(--muted); }
    .summary .pass { grid-area: pass; color: var(--muted); }
    .summary > button { grid-area: edit; }
  }

  .idx {
    color: var(--muted);
    font-size: 11px;
    font-variant-numeric: tabular-nums;
    text-align: center;
  }

  .kind {
    color: var(--label);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-size: 10px;
    font-weight: 600;
  }

  .endpoint {
    color: var(--text);
    font-weight: 500;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .worker, .wallet, .pass {
    color: var(--label);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .placeholder {
    grid-column: 3 / 7;
    color: var(--muted);
    font-style: italic;
    font-size: 11px;
  }

  .mono { font-family: ui-monospace, Menlo, monospace; font-size: 11px; }

  /* Edit form (inline) */
  .edit-form {
    padding: 12px 4px;
  }

  .edit-head {
    display: flex;
    align-items: center;
    gap: 12px;
    margin-bottom: 10px;
  }

  .fields {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
    gap: 10px 12px;
    margin-bottom: 10px;
  }

  .fields .narrow { max-width: 120px; }
  .fields .wide { grid-column: 1 / -1; }

  label {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }

  .lbl {
    font-size: 9px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    font-weight: 600;
  }

  input[type="text"], input[type="number"] {
    width: 100%;
    box-sizing: border-box;
    min-width: 0;
    padding: 7px 10px;
    background: var(--input);
    color: var(--text);
    border: 1px solid var(--border);
    border-radius: 4px;
    font-size: 12px;
    font-family: inherit;
    font-variant-numeric: tabular-nums;
    transition: border-color 0.15s;
  }

  input:focus { outline: none; border-color: var(--accent); }
  input:disabled { opacity: 0.5; cursor: not-allowed; }

  .actions {
    display: flex;
    align-items: center;
    gap: 10px;
    flex-wrap: wrap;
  }


  .msg {
    font-size: 11px;
    color: var(--success);
  }

  .msg.warn { color: var(--warning); }
</style>
