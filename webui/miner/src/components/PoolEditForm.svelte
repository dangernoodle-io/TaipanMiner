<script lang="ts">
  import Tooltip from './Tooltip.svelte'
  import Toggle from './Toggle.svelte'
  import PasswordInput from 'ui-kit/PasswordInput.svelte'

  type PoolForm = {
    host: string
    port: number
    wallet: string
    worker: string
    pool_pass: string
    extranonce_subscribe: boolean
    decode_coinbase: boolean
  }

  let {
    host = $bindable(),
    port = $bindable(),
    wallet = $bindable(),
    worker = $bindable(),
    pool_pass = $bindable(),
    extranonce_subscribe = $bindable(),
    decode_coinbase = $bindable(),
    kind,
    saving = false,
    saveMsg = '',
    workerPlaceholder = 'miner-1',
    onsave,
    oncancel,
  }: {
    host: string
    port: number
    wallet: string
    worker: string
    pool_pass: string
    extranonce_subscribe: boolean
    decode_coinbase: boolean
    kind: 'Primary' | 'Fallback'
    saving?: boolean
    saveMsg?: string
    workerPlaceholder?: string
    onsave?: () => void
    oncancel?: () => void
  } = $props()
</script>

<form class="setup-form" onsubmit={(e) => { e.preventDefault(); onsave?.() }}>
  <section>
    <h2>{kind} pool</h2>

    <div class="form-group">
      <div class="lbl-row">
        <label for="pool-host">Host</label>
        <Tooltip icon text="Stratum pool hostname or IP. Leave off the protocol — TCP only." />
      </div>
      <input id="pool-host" type="text" bind:value={host} maxlength="63" placeholder="pool.example.com" required disabled={saving} />
    </div>

    <div class="form-group">
      <div class="lbl-row">
        <label for="pool-port">Port</label>
        <Tooltip icon text="Stratum TCP port. Common values: 3333, 4334, 9999. Pool-specific — check the pool's docs." />
      </div>
      <input id="pool-port" type="number" bind:value={port} min="1" max="65535" placeholder="3333" required disabled={saving} />
    </div>

    <div class="form-group">
      <div class="lbl-row">
        <label for="pool-pass">Password</label>
        <Tooltip icon text="Pool password. Most pools accept any value (often 'x' or empty); some use it for worker config flags." />
      </div>
      <PasswordInput id="pool-pass" bind:value={pool_pass} placeholder="leave blank to keep current" disabled={saving} />
    </div>

    <div class="form-group">
      <div class="lbl-row">
        <label for="pool-wallet">Wallet</label>
        <Tooltip icon text="Bitcoin payout address registered with this pool. Most pools require a bech32 (bc1…) or legacy (1…/3…) address." />
      </div>
      <input id="pool-wallet" type="text" bind:value={wallet} spellcheck="false" required disabled={saving} />
    </div>

    <div class="form-group">
      <div class="lbl-row">
        <label for="pool-worker">Worker</label>
        <Tooltip icon text="Worker name appended to the wallet (wallet.worker) so the pool can track multiple miners separately. Defaults to the device hostname." />
      </div>
      <input id="pool-worker" type="text" bind:value={worker} placeholder={workerPlaceholder} required disabled={saving} />
    </div>
  </section>

  <details class="options disclosure">
    <summary>Advanced Options</summary>

    <div class="opt-row">
      <span class="opt-label">
        Extranonce subscribe
        <Tooltip icon text="Send mining.extranonce.subscribe after authorize so the pool can roll extranonce1 mid-session without forcing reconnect. Pools that don't support the extension reject it harmlessly." />
      </span>
      <Toggle bind:checked={extranonce_subscribe} disabled={saving} size="sm" />
    </div>

    <div class="opt-row">
      <span class="opt-label">
        Decode coinbase
        <Tooltip icon text="Decode coinbase tx for block height, scriptSig tag, payout, and reward. Turn off for non-BTC SHA-256d pools whose coinbase shape is unknown." />
      </span>
      <Toggle bind:checked={decode_coinbase} disabled={saving} size="sm" />
    </div>
  </details>

  <div class="actions">
    <button type="button" class="btn outline" onclick={() => oncancel?.()} disabled={saving}>Cancel</button>
    <button type="submit" class="btn primary" disabled={saving}>{saving ? 'Saving…' : 'Save'}</button>
  </div>
  {#if saveMsg}<div class="msg">{saveMsg}</div>{/if}
</form>

<!-- Form structure styles live in ui-kit/utilities.css under "forms" so
     FanEditDialog and other forms share the same look. -->
<style>
  /* .options layers pool-specific styling on top of the .disclosure utility:
     dashed top border + accent-colored bold summary + open-state column flow. */
  .options {
    border-top: 1px dashed var(--border);
    padding-top: 12px;
  }

  .options > summary {
    color: var(--accent);
    font-size: 14px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 1px;
    gap: 8px;
  }

  .options[open] {
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }

  .options[open] .opt-row + .opt-row {
    margin-top: 6px;
  }
</style>
