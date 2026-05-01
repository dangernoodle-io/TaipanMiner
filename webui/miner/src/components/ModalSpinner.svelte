<script lang="ts">
  export let visible: boolean = false
  export let label: string = ''
  export let sublabel: string = ''
</script>

{#if visible}
  <div class="modal-backdrop pool-backdrop" role="status" aria-live="polite" aria-busy="true">
    <div class="modal-panel pool-panel">
      <div class="spinner lg"></div>
      <h3>{label}</h3>
      {#if sublabel}<p>{sublabel}</p>{/if}
    </div>
  </div>
{/if}

<style>
  .pool-backdrop {
    display: flex;
    align-items: center;
    justify-content: center;
    background: rgba(0, 0, 0, 0.6);
    z-index: 100;
  }

  .pool-panel {
    padding: 32px 36px;
    width: min(420px, calc(100vw - 32px));
    text-align: center;
  }

  h3 {
    margin: 0 0 10px 0;
    color: var(--text);
    font-size: 16px;
    font-weight: 600;
  }

  p {
    margin: 0;
    color: var(--label);
    font-size: 13px;
    line-height: 1.5;
  }
</style>
