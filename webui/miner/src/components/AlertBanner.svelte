<script context="module" lang="ts">
  export interface Alert {
    key: string
    severity: 'danger' | 'warning' | 'info'
    message: string
  }
</script>

<script lang="ts">
  export let alerts: Alert[] = []
</script>

{#if alerts.length > 0}
  <div class="alert-banner">
    {#each alerts as alert (alert.key)}
      <div class="banner {alert.severity}">{alert.message}</div>
    {/each}
  </div>
{/if}

<style>
  .alert-banner {
    display: flex;
    flex-direction: column;
    gap: 8px;
    margin-bottom: 16px;
  }
</style>
