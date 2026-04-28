<script lang="ts">
  import { onMount } from 'svelte'
  import { stats, info, settings, pool } from '../lib/stores'
  import { fetchSettings, patchSettings, type Settings } from '../lib/api'
  import { fmtRelative } from '../lib/fmt'

  type PoolForm = { pool_host: string; pool_port: number; wallet: string; worker: string; pool_pass: string }

  let loading = true
  let loadErr = ''
  let saving = false
  let saveMsg = ''
  let rebootRequired = false
  let editingIdx: number | null = null  // 0 = primary, 1 = fallback

  let saved: Settings | null = null
  let form: PoolForm = { pool_host: '', pool_port: 0, wallet: '', worker: '', pool_pass: '' }

  // UI-only — fallback + rotation (firmware pending · TA-202 / TA-203)
  let fallbackConfigured = false
  let autoRotate = false
  let hostname = ''

  async function load() {
    loading = true
    loadErr = ''
    try {
      const s = await fetchSettings()
      saved = s
    } catch (e) {
      loadErr = (e as Error).message
    } finally {
      loading = false
    }
  }

  onMount(load)

  function startEdit(idx: number) {
    editingIdx = idx
    saveMsg = ''
    if (idx === 0 && saved) {
      form = {
        pool_host: saved.pool_host ?? '',
        pool_port: saved.pool_port ?? 0,
        wallet: saved.wallet ?? '',
        worker: saved.worker ?? '',
        pool_pass: saved.pool_pass ?? ''
      }
    } else {
      form = { pool_host: '', pool_port: 0, wallet: '', worker: '', pool_pass: '' }
    }
  }

  function cancelEdit() {
    editingIdx = null
    saveMsg = ''
  }

  async function handleSave() {
    if (editingIdx !== 0) return  // only primary is wired to firmware
    saveMsg = ''
    saving = true
    try {
      const res = await patchSettings({
        pool_host: form.pool_host.trim(),
        pool_port: form.pool_port,
        wallet: form.wallet.trim(),
        worker: form.worker.trim(),
        pool_pass: form.pool_pass
      })
      rebootRequired = res.reboot_required
      saveMsg = res.reboot_required ? 'Saved. Reboot required.' : 'Saved.'
      editingIdx = null
      await load()
    } catch (e) {
      saveMsg = `Save failed: ${(e as Error).message}`
    } finally {
      saving = false
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
    <h3>Active</h3>
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
          {#if $pool?.session_start_ago_s != null}
            · session {fmtRelative($pool.session_start_ago_s)}
          {/if}
        </div>
      </div>
      <div class="metrics">
        <div class="m">
          <div class="v">{$pool?.current_difficulty ?? '—'}</div>
          <div class="k">diff</div>
        </div>
        <div class="m">
          <div class="v">{$stats ? fmtRelative($stats.last_share_ago_s) : '—'}</div>
          <div class="k">last share</div>
        </div>
        <div class="m">
          {#if $stats}
            <div class="v">{$stats.session_shares}<span class="sep">/</span><span class="rej">{$stats.session_rejected}</span></div>
          {:else}
            <div class="v">—</div>
          {/if}
          <div class="k">shares</div>
        </div>
      </div>
    </div>
  </section>

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

    {#if loading}
      <div class="loading">Loading…</div>
    {:else if loadErr}
      <div class="error">{loadErr}</div>
    {:else}
      <div class="pool-list">
        <!-- Primary -->
        <div class="pool-row" class:editing={editingIdx === 0}>
          {#if editingIdx !== 0}
            <div class="summary">
              <span class="idx">1</span>
              <span class="kind">Primary</span>
              <span class="endpoint">{saved?.pool_host || '—'}{#if saved?.pool_port}:{saved.pool_port}{/if}</span>
              <span class="worker">{saved?.worker || '—'}</span>
              <span class="wallet mono" title={saved?.wallet}>{truncWallet(saved?.wallet)}</span>
              <span class="pass">{saved?.pool_pass ? '••••' : '—'}</span>
              <button class="btn outline sm" on:click={() => startEdit(0)}>Edit</button>
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
                  <input type="text" bind:value={form.pool_host} maxlength="63" required />
                </label>
                <label class="narrow">
                  <span class="lbl">Port</span>
                  <input type="number" bind:value={form.pool_port} min="1" max="65535" required />
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
        <div class="pool-row" class:editing={editingIdx === 1} class:disabled={!fallbackConfigured}>
          {#if editingIdx !== 1}
            <div class="summary">
              <span class="idx">2</span>
              <span class="kind">Fallback</span>
              {#if fallbackConfigured}
                <span class="endpoint">—</span>
                <span class="worker">—</span>
                <span class="wallet">—</span>
                <span class="pass">—</span>
                <button class="btn outline sm" on:click={() => startEdit(1)} disabled>Edit</button>
              {:else}
                <span class="placeholder">not configured · optional second pool for failover</span>
                <button class="btn outline sm" on:click={() => { fallbackConfigured = true; startEdit(1) }} disabled>+ Add</button>
                <span class="pending-tag">TA-202</span>
              {/if}
            </div>
          {:else}
            <form class="edit-form" on:submit|preventDefault>
              <div class="edit-head">
                <span class="idx">2</span>
                <span class="kind">Fallback</span>
                <span class="pending-tag">firmware pending · TA-202</span>
              </div>
              <div class="fields">
                <label>
                  <span class="lbl">Host</span>
                  <input type="text" bind:value={form.pool_host} disabled />
                </label>
                <label class="narrow">
                  <span class="lbl">Port</span>
                  <input type="number" bind:value={form.pool_port} disabled />
                </label>
                <label>
                  <span class="lbl">Worker</span>
                  <input type="text" bind:value={form.worker} disabled />
                </label>
                <label class="wide">
                  <span class="lbl">Wallet</span>
                  <input type="text" bind:value={form.wallet} disabled />
                </label>
                <label>
                  <span class="lbl">Password</span>
                  <input type="text" bind:value={form.pool_pass} disabled />
                </label>
              </div>
              <div class="actions">
                <button type="button" class="btn outline sm" on:click={cancelEdit}>Cancel</button>
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

  .m .sep { color: var(--muted); margin: 0 3px; }
  .m .rej { color: var(--warning); }

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
  .fields .wide { grid-column: span 2; }

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
