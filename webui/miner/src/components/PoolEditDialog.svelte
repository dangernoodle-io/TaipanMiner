<script lang="ts">
  import PoolEditForm from './PoolEditForm.svelte'

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
    open = false,
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
    open?: boolean
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

  function onBackdrop() {
    if (!saving) oncancel?.()
  }
</script>

{#if open}
  <div class="modal-backdrop" onclick={onBackdrop} role="presentation"></div>
  <div class="modal-panel dialog" role="dialog" aria-modal="true" aria-labelledby="pool-edit-title">
    <PoolEditForm
      bind:host
      bind:port
      bind:wallet
      bind:worker
      bind:pool_pass
      bind:extranonce_subscribe
      bind:decode_coinbase
      {kind}
      {saving}
      {saveMsg}
      {workerPlaceholder}
      {onsave}
      {oncancel}
    />
  </div>
{/if}

<style>
  .dialog {
    position: fixed;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    padding: 24px 26px;
    width: min(480px, calc(100vw - 32px));
    max-height: calc(100vh - 32px);
    overflow-y: auto;
    z-index: 41;
  }
</style>
