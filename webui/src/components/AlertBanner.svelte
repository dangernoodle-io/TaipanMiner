<script lang="ts">
  import { stats, connected, power, pool } from '../lib/stores'

  interface Alert {
    key: string
    severity: 'error' | 'warn' | 'info'
    message: string
  }

  let alerts: Alert[] = []

  $: {
    const newAlerts: Alert[] = []

    if (!$connected) {
      newAlerts.push({
        key: 'disconnected',
        severity: 'error',
        message: 'Miner unreachable'
      })
    }

    if ($stats?.asic_temp_c && $stats.asic_temp_c > 75) {
      newAlerts.push({
        key: 'temp',
        severity: 'warn',
        message: `High temperature: ${$stats.asic_temp_c.toFixed(1)}°C`
      })
    }

    if ($pool && !$pool.current_difficulty) {
      newAlerts.push({
        key: 'pool_diff',
        severity: 'info',
        message: 'Waiting for pool difficulty'
      })
    }

    alerts = newAlerts
  }
</script>

{#if alerts.length > 0}
  <div class="alert-banner">
    {#each alerts as alert (alert.key)}
      <div class="alert" class:error={alert.severity === 'error'} class:warn={alert.severity === 'warn'} class:info={alert.severity === 'info'}>
        <span class="severity">{alert.severity.toUpperCase()}</span>
        <span class="message">{alert.message}</span>
      </div>
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

  .alert {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 12px 16px;
    border-radius: 6px;
    border-left: 4px solid;
    background: var(--surface);
    font-size: 13px;
  }

  .alert.error {
    border-left-color: var(--danger);
  }

  .alert.warn {
    border-left-color: var(--warning);
  }

  .alert.info {
    border-left-color: var(--info);
  }

  .severity {
    font-weight: bold;
    font-size: 11px;
    letter-spacing: 0.5px;
  }

  .message {
    color: var(--label);
  }
</style>
