<script lang="ts">
  import { createEventDispatcher } from 'svelte'
  import PoolEditForm from './PoolEditForm.svelte'
  import type { Pool, PoolConfigured } from '../lib/api'
  import { truncWallet } from '../lib/fmt'
  import { coinbaseHeight } from '../lib/coinbase'

  type PoolForm = {
    host: string
    port: number
    wallet: string
    worker: string
    pool_pass: string
    extranonce_subscribe: boolean
    decode_coinbase: boolean
  }

  // Which slot this row represents.
  export let idx: 0 | 1
  // Source of truth for the displayed pool (frozen during a switch).
  export let displayPool: Pool | null
  // True when THIS row is in edit mode.
  export let editing: boolean = false
  // Form state, bound back to parent so handleSave can read it.
  export let form: PoolForm
  export let saving: boolean = false
  export let saveMsg: string = ''
  export let switching: boolean = false
  export let workerPlaceholder: string = 'miner-1'

  $: kind = (idx === 0 ? 'Primary' : 'Fallback') as 'Primary' | 'Fallback'
  $: cfg = idx === 0 ? displayPool?.configured?.primary : displayPool?.configured?.fallback
  $: otherCfg = idx === 0 ? displayPool?.configured?.fallback : displayPool?.configured?.primary
  $: isActive = displayPool?.active_pool_idx === idx && displayPool?.connected
  // The "switch" button shows on the inactive row when we have somewhere to switch from.
  $: canSwitchTo = !isActive && cfg && displayPool?.active_pool_idx === (idx === 0 ? 1 : 0) && otherCfg
  // Primary can be removed iff fallback is configured (so it can be promoted).
  // Fallback can always be removed when configured.
  $: canRemove = cfg && (idx === 1 || !!displayPool?.configured?.fallback)
  $: removeTitle = idx === 0
    ? 'Remove primary; fallback will be promoted'
    : 'Remove fallback pool'

  /* Status pill text/class for the per-toggle indicators on the summary
   * row. For the active pool we surface runtime state from the pool
   * snapshot. For inactive pools we only know the persisted user pref
   * (ON/OFF). */
  $: subscribeStatus = (() => {
    const enabled = cfg?.extranonce_subscribe ?? false
    if (!isActive) return enabled ? { text: 'ON', cls: 'on' } : { text: 'OFF', cls: 'off' }
    const runtime = displayPool?.extranonce_subscribe_status ?? 'off'
    return { text: runtime.toUpperCase(), cls: runtime }
  })()

  $: decodeStatus = (() => {
    const enabled = cfg?.decode_coinbase ?? true
    if (!enabled) return { text: 'OFF', cls: 'off' }
    if (!isActive) return { text: 'ON', cls: 'on' }
    /* Active + flag on: parse-failed if notify is non-empty but coinbaseHeight
     * returns null (same signal as the inline check). */
    const n = displayPool?.notify
    if (n && n.coinb1 && n.coinb1.length >= 84) {
      if (coinbaseHeight(n.coinb1) == null) return { text: 'PARSE FAILED', cls: 'rejected' }
    }
    return { text: 'ACTIVE', cls: 'active' }
  })()

  const dispatch = createEventDispatcher<{
    edit: void
    'cancel-edit': void
    save: void
    switch: void
    remove: void
  }>()


</script>

<div class="pool-row" class:editing class:disabled={!cfg && !editing}>
  {#if !editing}
    <div class="summary">
      {#if cfg}
        <div class="info">
          <div class="caption-row">
            <span class="kind-caption">{kind}</span>
            {#if isActive}
              <span class="active-tag">ACTIVE</span>
            {/if}
          </div>
          <div class="endpoint-line">
            <span class="ep-field">
              <span class="ep-key">host</span>
              <span class="ep-host">{cfg.host}</span>
            </span>
            {#if cfg.port}
              <span class="ep-field">
                <span class="ep-key">port</span>
                <span class="ep-port">{cfg.port}</span>
              </span>
            {/if}
            <span class="ep-field">
              <span class="ep-key">worker</span>
              <span class="ep-worker">{cfg.worker}</span>
            </span>
            <span class="ep-field">
              <span class="ep-key">wallet</span>
              <span class="ep-wallet mono" title={cfg.wallet}>{truncWallet(cfg.wallet)}</span>
            </span>
          </div>
          <div class="settings-line">
            <span class="setting-indicator" title="Send mining.extranonce.subscribe after authorize so the pool can roll extranonce1 mid-session without forcing a reconnect. Pools that don't support the extension just reject the request — harmless.">
              <span class="setting-label">extranonce subscribe</span>
              <span class="status-pill {subscribeStatus.cls}">{subscribeStatus.text}</span>
            </span>
            <span class="setting-indicator" title="Decode this pool's coinbase tx for block height, scriptSig tag, payout address, and reward. Turn off for non-BTC SHA-256d pools whose coinbase shape we don't understand — those would crash or return garbage from the parser.">
              <span class="setting-label">decode coinbase</span>
              <span class="status-pill {decodeStatus.cls}">{decodeStatus.text}</span>
            </span>
          </div>
        </div>
        <div class="actions">
          {#if canSwitchTo}
            <button class="btn outline sm" on:click={() => dispatch('switch')} disabled={switching}>{switching ? 'Switching…' : 'Switch'}</button>
          {/if}
          <button class="btn outline sm" on:click={() => dispatch('edit')}>Edit</button>
          {#if canRemove}
            <button class="btn outline sm danger" on:click={() => dispatch('remove')} disabled={saving} title={removeTitle}>Remove</button>
          {/if}
        </div>
      {:else}
        <div class="info">
          <div class="kind-caption">{kind}</div>
          <div class="placeholder">
            {idx === 0 ? 'not configured' : 'not configured · optional second pool for failover'}
          </div>
        </div>
        <div class="actions">
          <button class="btn outline sm" on:click={() => dispatch('edit')}>{idx === 0 ? 'Configure' : '+ Add'}</button>
        </div>
      {/if}
    </div>
  {:else}
    <PoolEditForm
      bind:form
      {kind}
      {saving}
      {saveMsg}
      {workerPlaceholder}
      on:save={() => dispatch('save')}
      on:cancel={() => dispatch('cancel-edit')}
    />
  {/if}
</div>

<style>
  .pool-row + :global(.pool-row) {
    border-top: 1px dashed var(--border);
  }

  .pool-row.disabled { opacity: 0.5; }
  .pool-row.editing { background: var(--bg); }

  .summary {
    display: flex;
    align-items: flex-start;
    gap: 12px;
    padding: 12px 4px;
    font-size: 12px;
  }

  .summary .info {
    flex: 1 1 auto;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 2px;
  }

  .summary .caption-row {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 2px;
  }

  .summary .caption-row .kind-caption { margin-bottom: 0; }

  .summary .kind-caption {
    color: var(--label);
    text-transform: uppercase;
    letter-spacing: 0.6px;
    font-size: 11px;
    font-weight: 700;
    margin-bottom: 2px;
  }

  .summary .endpoint-line {
    display: flex;
    flex-wrap: wrap;
    align-items: baseline;
    gap: 6px 22px;
    min-width: 0;
  }

  .summary .ep-field {
    display: inline-flex;
    align-items: baseline;
    gap: 6px;
    min-width: 0;
    overflow: hidden;
  }

  .summary .ep-key {
    flex-shrink: 0;
    color: var(--label);
    font-size: 9px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    opacity: 0.7;
  }

  .summary .ep-host,
  .summary .ep-port,
  .summary .ep-worker,
  .summary .ep-wallet {
    color: var(--text);
    font-size: 13px;
    font-weight: 500;
    font-variant-numeric: tabular-nums;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    min-width: 0;
  }

  .summary .ep-host { font-weight: 600; }

  .summary .actions {
    display: flex;
    align-items: center;
    gap: 8px;
    flex-shrink: 0;
    /* Visually align the action cluster with the endpoint line, not the
     * caption row above it — the caption is small and uppercase and
     * looks awkward sharing baseline with primary buttons. */
    margin-top: 16px;
  }

  .placeholder {
    color: var(--muted);
    font-style: italic;
    font-size: 11px;
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

  /* Per-toggle status indicators (read-only on the summary row;
   * mutation lives in the edit form). */
  .settings-line {
    display: flex;
    flex-wrap: wrap;
    gap: 12px;
    margin-top: 4px;
  }

  .setting-indicator {
    display: inline-flex;
    align-items: center;
    gap: 5px;
    font-size: 9px;
    text-transform: uppercase;
    letter-spacing: 0.4px;
  }

  .setting-label { color: var(--label); font-weight: 600; }

  .status-pill {
    display: inline-block;
    font-size: 8px;
    font-weight: 600;
    letter-spacing: 0.4px;
    padding: 1px 4px;
    border-radius: 2px;
    font-variant-numeric: tabular-nums;
    color: var(--muted);
    background: color-mix(in srgb, var(--muted) 10%, transparent);
    border: 1px solid color-mix(in srgb, var(--muted) 30%, transparent);
  }
  .status-pill.active, .status-pill.on {
    color: var(--success);
    background: color-mix(in srgb, var(--success) 12%, transparent);
    border-color: color-mix(in srgb, var(--success) 50%, transparent);
  }
  .status-pill.rejected {
    color: var(--warning);
    background: color-mix(in srgb, var(--warning) 12%, transparent);
    border-color: color-mix(in srgb, var(--warning) 50%, transparent);
  }
  .status-pill.off {
    color: var(--muted);
    background: color-mix(in srgb, var(--muted) 6%, transparent);
    border-color: color-mix(in srgb, var(--muted) 35%, transparent);
  }

  /* Outline-style danger override; the global .btn.danger fills the
   * background which collides with the red text. We want a quieter
   * destructive button on a row that already shows lots of state. */
  .btn.danger {
    background: transparent;
    color: var(--danger);
    border-color: color-mix(in srgb, var(--danger) 50%, transparent);
  }
  .btn.danger:hover:not(:disabled) {
    filter: none;
    background: color-mix(in srgb, var(--danger) 12%, transparent);
  }

  .mono { font-family: ui-monospace, Menlo, monospace; font-size: 11px; }

  @media (max-width: 720px) {
    .summary {
      flex-wrap: wrap;
      row-gap: 8px;
    }
    .summary .info {
      flex-basis: 100%;
      order: 3;
    }
    .summary .actions {
      flex-basis: 100%;
      justify-content: flex-end;
      order: 4;
    }
    .summary .endpoint-line { font-size: 13px; }
    /* On narrow widths, lay the four fields out as a 2-column grid so
     * port aligns under host and wallet aligns under worker. */
    .summary .endpoint-line {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 6px 18px;
    }
  }
</style>
