<script lang="ts">
  import { onMount } from 'svelte'
  import { stats, info, pool } from '../lib/stores'
  import { fetchPool, putPool, switchPool, deletePoolSlot, type PoolConfigured, type PoolConfigInput, type PoolPutBody } from '../lib/api'
  import { fmtRelative, fmtNetDiff, fmtBtc, fmtNtimeAge, truncAddr } from '../lib/fmt'
  import { nbitsToDifficulty, coinbaseTag, coinbaseHeight, coinbaseTotalReward, coinbasePayoutSpk, segwitAddress } from '../lib/coinbase'
  import PoolRow from '../components/PoolRow.svelte'
  import ConfirmDialog from '../components/ConfirmDialog.svelte'
  import ModalSpinner from '../components/ModalSpinner.svelte'

  const POOL_IDXS: (0 | 1)[] = [0, 1]

  type PoolForm = {
    host: string
    port: number
    wallet: string
    worker: string
    pool_pass: string
    extranonce_subscribe: boolean  // TA-306
    decode_coinbase: boolean       // TA-307
  }

  let saving = false
  let saveMsg = ''
  let editingIdx: number | null = null  // 0 = primary, 1 = fallback
  let switching = false
  // ConfirmDialog state for handleRemove
  let removeConfirmOpen = false
  let removeConfirmMsg = ''
  let pendingRemoveSlot: 'primary' | 'fallback' | null = null
  // Same overlay machinery as switching, but flipped on after a save that
  // forces a fresh stratum session (i.e. saving the active slot).
  let reconnecting = false
  // Frozen snapshot of $pool taken at switch-click; rendered in place of the
  // live store while switching so the page doesn't flicker as the firmware
  // tears down the old session. Cleared once the new session is observed.
  let frozenPool: typeof $pool | null = null
  $: displayPool = (switching || reconnecting) ? frozenPool : $pool

  /* TA-307: per-pool flag for the active session controls UI coinbase
   * decoding. Defaults to true when no active pool / no config so the
   * tiles render normally pre-connect. */
  $: activeDecodeCoinbase = (() => {
    const idx = displayPool?.active_pool_idx
    if (idx === 0) return displayPool?.configured?.primary?.decode_coinbase ?? true
    if (idx === 1) return displayPool?.configured?.fallback?.decode_coinbase ?? true
    return true
  })()

  /* Parse-failed signal: flag is on, notify is non-empty, but no parser
   * recognized any coinbase field. Used to badge the toggle so the user
   * knows the tiles vanished because the parser couldn't read this pool's
   * shape, not because they turned the flag off. */
  $: coinbaseParseFailed = (() => {
    if (!activeDecodeCoinbase) return false
    const n = displayPool?.notify
    if (!n || !n.coinb1 || n.coinb1.length < 84) return false
    return coinbaseHeight(n.coinb1) == null
        && coinbaseTotalReward(n.coinb2) == null
        && coinbasePayoutSpk(n.coinb2) == null
  })()

  let form: PoolForm = { host: '', port: 0, wallet: '', worker: '', pool_pass: '', extranonce_subscribe: false, decode_coinbase: true }
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
        pool_pass: '',
        extranonce_subscribe: cfg.extranonce_subscribe ?? false,
        decode_coinbase: cfg.decode_coinbase ?? true,
      }
    } else {
      form = { host: '', port: 0, wallet: '', worker: '', pool_pass: '', extranonce_subscribe: false, decode_coinbase: true }
    }
  }

  function cancelEdit() {
    editingIdx = null
    saveMsg = ''
  }

  // Build a PUT slot for a non-edited pool by mirroring its current state.
  // Omits pool_pass — firmware preserves it on missing key (see PR #280).
  function slotFromCurrent(c: NonNullable<PoolConfigured>): PoolConfigInput {
    const next: PoolConfigInput = {
      host: c.host,
      port: c.port,
      worker: c.worker,
      wallet: c.wallet,
      pool_pass: '',
      extranonce_subscribe: c.extranonce_subscribe ?? false,
      decode_coinbase: c.decode_coinbase ?? true,
    }
    delete (next as Partial<PoolConfigInput>).pool_pass
    return next
  }

  // Build a PUT slot from the form (the edited pool).
  function slotFromForm(): PoolConfigInput {
    return {
      host: form.host.trim(),
      port: form.port,
      worker: form.worker.trim(),
      wallet: form.wallet.trim(),
      pool_pass: form.pool_pass,
      extranonce_subscribe: form.extranonce_subscribe,
      decode_coinbase: form.decode_coinbase,
    }
  }

  async function handleSave() {
    if (editingIdx === null) return
    saveMsg = ''
    saving = true
    /* If editing the currently-active slot, the save needs to drive a
     * fresh stratum session before the user-visible state lines up with
     * what they just entered. Mirror handleSwitch's freeze-and-poll so
     * the page doesn't flicker with stale values during the reconnect. */
    const editingActive = $pool?.active_pool_idx === editingIdx && $pool?.connected
    const preAge = editingActive ? ($pool?.session_start_ago_s ?? null) : null
    if (editingActive) {
      frozenPool = $pool
      reconnecting = true
    }
    try {
      const cfg = $pool?.configured
      const edited = slotFromForm()
      const body: PoolPutBody = {
        primary: editingIdx === 0
          ? edited
          : (cfg?.primary ? slotFromCurrent(cfg.primary) : edited),
        fallback: editingIdx === 1
          ? edited
          : (cfg?.fallback ? slotFromCurrent(cfg.fallback) : null),
      }
      await putPool(body)
      saveMsg = 'Saved.'
      editingIdx = null

      if (editingActive) {
        /* Wait for the firmware to bring up a fresh session under the new
         * config. Same shape as handleSwitch — bounded poll, fresh-session
         * detector via session_start_ago_s shrinking. */
        const deadline = Date.now() + 15000
        while (Date.now() < deadline) {
          await new Promise(r => setTimeout(r, 750))
          const p = await fetchPool()
          pool.set(p)
          if (!p.connected) continue
          if (p.session_start_ago_s == null) continue
          if (preAge == null || p.session_start_ago_s < preAge) break
        }
      } else {
        pool.set(await fetchPool())
      }
    } catch (e) {
      saveMsg = `Save failed: ${(e as Error).message}`
    } finally {
      saving = false
      reconnecting = false
      frozenPool = null
    }
  }

  function handleRemove(slot: 'primary' | 'fallback') {
    const isPromote = slot === 'primary'
    removeConfirmMsg = isPromote
      ? 'Remove the primary pool? The fallback will be promoted to primary.'
      : 'Remove the fallback pool? Auto-failover will be disabled.'
    pendingRemoveSlot = slot
    removeConfirmOpen = true
  }

  async function doRemove() {
    if (!pendingRemoveSlot) return
    const slot = pendingRemoveSlot
    pendingRemoveSlot = null
    saveMsg = ''
    saving = true
    // Freeze the view while the firmware reshuffles slots, similar to switch.
    frozenPool = $pool
    try {
      await deletePoolSlot(slot)
      pool.set(await fetchPool())
    } catch (e) {
      saveMsg = `Remove failed: ${(e as Error).message}`
    } finally {
      saving = false
      frozenPool = null
    }
  }

  async function handleSwitch(idx: 0 | 1) {
    // Freeze the displayed pool BEFORE flipping `switching` so the first
    // reactive tick already sees a stable view.
    frozenPool = $pool
    // Snapshot pre-switch session age. The firmware flips active_pool_idx
    // synchronously and only tears down the stratum socket on the next loop
    // iteration, so checking idx+connected post-call returns true instantly
    // while the *old* session is still up. Wait for a fresh session
    // (new session_start_ago_s smaller than the pre-switch value).
    const preAge = $pool?.session_start_ago_s ?? null
    switching = true
    try {
      await switchPool(idx)
      const deadline = Date.now() + 15000
      while (Date.now() < deadline) {
        await new Promise(r => setTimeout(r, 750))
        const p = await fetchPool()
        pool.set(p)
        if (p.active_pool_idx !== idx) continue
        if (!p.connected) continue
        if (p.session_start_ago_s == null) continue
        if (preAge == null || p.session_start_ago_s < preAge) break
      }
    } catch (e) {
      saveMsg = `Switch failed: ${(e as Error).message}`
    } finally {
      switching = false
      frozenPool = null
    }
  }

</script>

<div class="pool-grid" class:is-switching={switching || reconnecting}>
  <ModalSpinner
    visible={switching || reconnecting}
    label={reconnecting ? 'Reconnecting…' : 'Switching pools…'}
    sublabel={reconnecting ? 'applying changes' : 'reconnecting stratum'}
  />
  <!-- Active pool status — read-only metrics from /api/pool (TA-281). -->
  <section class="card active">
    <header class="active-head">
      <h3>Active</h3>
      {#if activeDecodeCoinbase && displayPool?.notify && coinbaseTag(displayPool.notify.coinb1, displayPool.notify.coinb2)}
        <span class="pool-tag has-tip">
          <span class="tag-prefix">scriptSig</span>
          {coinbaseTag(displayPool.notify.coinb1, displayPool.notify.coinb2)}
          <span class="tip" role="tooltip">
            Block template upstream, read from the coinbase scriptSig. Not necessarily the stratum endpoint you're connected to — proxies and relays often forward another pool's template.
          </span>
        </span>
      {/if}
    </header>
    <div class="status-row">
      <div class="who">
        <div class="host">
          <span class="conn-dot" class:connected={displayPool?.connected === true}
                               class:disconnected={displayPool?.connected === false}
                               class:unknown={displayPool == null}
                aria-hidden="true"></span>
          {displayPool?.host ?? '—'}:{displayPool?.port ?? '—'}
        </div>
        <div class="sub">
          worker {displayPool?.worker ?? '—'}
        </div>
      </div>
      <div class="metrics">
        <div class="m">
          <div class="v">{displayPool?.current_difficulty ?? '—'}</div>
          <div class="k">diff</div>
        </div>
        <div class="m">
          <div class="v">{displayPool?.session_start_ago_s != null ? fmtRelative(displayPool.session_start_ago_s) : '—'}</div>
          <div class="k">session</div>
        </div>
        <div class="m">
          <div class="v">{displayPool?.latency_ms != null ? `${displayPool.latency_ms} ms` : '—'}</div>
          <div class="k">latency</div>
        </div>
      </div>
    </div>
    {#if displayPool?.extranonce1 || displayPool?.version_mask}
      <div class="card-footer">
        {#if displayPool?.extranonce1}
          <div class="sf">
            <div class="field-key">extranonce1</div>
            <div class="field-val mono" title="server-assigned per-session nonce prefix">
              {displayPool.extranonce1}{displayPool.extranonce2_size != null ? ` · ${displayPool.extranonce2_size}B en2` : ''}
            </div>
          </div>
        {/if}
        {#if displayPool?.version_mask}
          <div class="sf">
            <div class="field-key">version mask</div>
            <div class="field-val mono" title="BIP-320 version-rolling bits">0x{displayPool.version_mask}</div>
          </div>
        {/if}
      </div>
    {/if}
  </section>

  <!-- Stratum notify preview (TA-288). -->
  {#if displayPool?.notify}
    {@const n = displayPool.notify}
    <section class="card stratum">
      <header class="stratum-head">
        <h3>Current Job</h3>
        <span class="job-id" title="job_id">#{n.job_id}</span>
        {#if n.clean_jobs}
          <span class="clean" title="clean_jobs flag set in mining.notify">CLEAN</span>
        {/if}
      </header>
      <div class="stratum-grid">
        {#if activeDecodeCoinbase && coinbaseHeight(n.coinb1) != null}
          <div class="sf">
            <div class="sk">block height</div>
            <div class="sv mono">{fmtNetDiff(coinbaseHeight(n.coinb1) ?? 0)}</div>
          </div>
        {/if}
        {#if activeDecodeCoinbase && coinbaseTotalReward(n.coinb2) != null}
          <div class="sf">
            <div class="sk">block reward</div>
            <div class="sv mono">{fmtBtc(coinbaseTotalReward(n.coinb2) ?? 0)}</div>
          </div>
        {/if}
        <div class="sf">
          <div class="sk">merkle depth</div>
          <div class="sv mono">{n.merkle_branches.length}</div>
        </div>
        <div class="sf">
          <div class="sk">network diff</div>
          <div class="sv mono">{fmtNetDiff(nbitsToDifficulty(n.nbits))}</div>
        </div>
        <div class="sf">
          <div class="sk">prev block</div>
          <div class="sv mono" title={n.prev_hash}>
            {n.prev_hash.slice(0, 8)}…{n.prev_hash.slice(-8)}
          </div>
        </div>
        {#if fmtNtimeAge(n.ntime)}
          <div class="sf">
            <div class="sk">template age</div>
            <div class="sv mono">{fmtNtimeAge(n.ntime)}</div>
          </div>
        {/if}
        <div class="sf">
          <div class="sk">version</div>
          <div class="sv mono">0x{n.version}</div>
        </div>
      </div>
      {#if activeDecodeCoinbase && coinbasePayoutSpk(n.coinb2)}
        {@const spk = coinbasePayoutSpk(n.coinb2)}
        {@const addr = spk ? segwitAddress(spk) : null}
        <div class="card-footer">
          <span class="field-key">payout</span>
          <span class="field-val mono" title={addr ?? spk ?? ''}>
            {addr ?? 'non-segwit ' + (spk ?? '')}
          </span>
        </div>
      {/if}
    </section>
  {/if}

  <!-- Pool rows -->
  <section class="card pools">
    <header class="pools-head">
      <h3>Pools</h3>
      <label class="rotate" class:disabled-ctrl={true}>
        <input type="checkbox" bind:checked={autoRotate} disabled />
        <span>Auto rotate</span>
        <span class="tag pending">TA-203</span>
      </label>
    </header>

    {#if displayPool}
      <div class="pool-list">
        {#each (POOL_IDXS) as idx (idx)}
          <PoolRow
            idx={idx}
            displayPool={displayPool}
            editing={editingIdx === idx}
            bind:form
            {saving}
            {saveMsg}
            {switching}
            workerPlaceholder={hostname || $info?.worker_name || 'miner-1'}
            on:edit={() => startEdit(idx)}
            on:cancel-edit={cancelEdit}
            on:save={handleSave}
            on:switch={() => handleSwitch(idx)}
            on:remove={() => handleRemove(idx === 0 ? 'primary' : 'fallback')}
          />
        {/each}
      </div>
    {/if}
  </section>
</div>

<ConfirmDialog
  open={removeConfirmOpen}
  title="Remove pool?"
  message={removeConfirmMsg}
  confirmLabel="Remove"
  danger
  on:confirm={() => { removeConfirmOpen = false; doRemove() }}
  on:cancel={() => { removeConfirmOpen = false; pendingRemoveSlot = null }}
/>

<style>
  .pool-grid {
    display: grid;
    grid-template-columns: 1fr;
    gap: 14px;
    position: relative;
  }

  .pool-grid.is-switching > :global(section.card) {
    filter: blur(1px);
    pointer-events: none;
    user-select: none;
  }

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


  .pool-tag.has-tip {
    position: relative;
    cursor: help;
    overflow: visible;
    max-width: none;
  }

  .has-tip .tip {
    position: absolute;
    top: calc(100% + 6px);
    left: 0;
    z-index: 50;
    width: max-content;
    max-width: 280px;
    padding: 8px 10px;
    border-radius: 6px;
    background: var(--bg-elevated, #1f1f1f);
    color: var(--text);
    border: 1px solid var(--border);
    box-shadow: 0 6px 16px rgba(0, 0, 0, 0.35);
    font-size: 11px;
    font-weight: 400;
    line-height: 1.45;
    text-transform: none;
    letter-spacing: 0;
    white-space: normal;
    opacity: 0;
    pointer-events: none;
    transform: translateY(-2px);
    transition: opacity 80ms ease-out, transform 80ms ease-out;
  }

  .has-tip:hover .tip,
  .has-tip:focus-within .tip {
    opacity: 1;
    transform: translateY(0);
    transition-delay: 300ms;
  }

  .pool-tag .tag-prefix {
    font-size: 9px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    font-weight: 700;
    margin-right: 6px;
    opacity: 0.85;
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

  /* session / payout strip: sf children need min-width guard for long mono values */
  .card-footer .sf { min-width: 0; }
  .card-footer .sf:last-child { text-align: right; }
  /* mono field-val values in strips need overflow clipping */
  .card-footer .field-val {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    min-width: 0;
  }

  @media (max-width: 720px) {
    .card-footer .sf:last-child { text-align: left; }
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

  /* card h3 typography lives in ui-kit utilities.css; this page's h3s sit
     inside flex headers that handle their own spacing, so margin: 0 from
     the shared rule is correct as-is. */

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

  /* Pool list — rows + edit form live in components/PoolRow + PoolEditForm. */
  .pool-list {
    display: flex;
    flex-direction: column;
  }

  .mono { font-family: ui-monospace, Menlo, monospace; font-size: 11px; }
</style>
