<script lang="ts">
  import { createEventDispatcher } from 'svelte'

  // Form shape lives in the parent; we bind to it so changes propagate.
  type PoolForm = {
    host: string
    port: number
    wallet: string
    worker: string
    pool_pass: string
    extranonce_subscribe: boolean
    decode_coinbase: boolean
  }

  export let form: PoolForm
  export let kind: 'Primary' | 'Fallback'
  export let saving: boolean = false
  export let saveMsg: string = ''
  export let workerPlaceholder: string = 'miner-1'

  const dispatch = createEventDispatcher<{ save: void; cancel: void }>()
</script>

<form class="edit-form" on:submit|preventDefault={() => dispatch('save')}>
  <div class="edit-head">
    <span class="kind">{kind}</span>
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
      <input type="text" bind:value={form.worker} placeholder={workerPlaceholder} required />
    </label>
    <label class="wide">
      <span class="lbl">Wallet</span>
      <input type="text" bind:value={form.wallet} spellcheck="false" required />
    </label>
    <label>
      <span class="lbl">Password</span>
      <input type="text" bind:value={form.pool_pass} placeholder="x" />
    </label>
    <label class="checkbox wide" title="Send mining.extranonce.subscribe after authorize. Pools that don't support the extension just reject the request — harmless.">
      <input type="checkbox" bind:checked={form.extranonce_subscribe} />
      <span class="lbl">extranonce subscribe</span>
    </label>
    <label class="checkbox wide" title="Decode coinbase tx for block height, scriptSig tag, payout, and reward. Turn off for non-BTC SHA-256d pools whose coinbase shape we don't understand.">
      <input type="checkbox" bind:checked={form.decode_coinbase} />
      <span class="lbl">decode coinbase</span>
    </label>
  </div>
  <div class="actions">
    <button type="submit" class="btn primary sm" disabled={saving}>{saving ? 'Saving…' : 'Save'}</button>
    <button type="button" class="btn outline sm" on:click={() => dispatch('cancel')} disabled={saving}>Cancel</button>
    {#if saveMsg}<span class="msg">{saveMsg}</span>{/if}
  </div>
</form>

<style>
  .edit-form {
    padding: 12px 4px;
  }

  .edit-head {
    display: flex;
    align-items: center;
    gap: 12px;
    margin-bottom: 10px;
  }

  .edit-head .kind {
    color: var(--label);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-size: 10px;
    font-weight: 600;
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

  label.checkbox {
    flex-direction: row;
    align-items: center;
    gap: 8px;
    cursor: pointer;
    user-select: none;
  }

  label.checkbox .lbl { margin-bottom: 0; }

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

</style>
