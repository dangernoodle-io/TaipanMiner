<script lang="ts">
  import { stats, connected, power, pool } from '../lib/stores'

  interface Alert {
    key: string
    severity: 'danger' | 'warning' | 'info'
    message: string
  }

  let alerts: Alert[] = []

  $: {
    const newAlerts: Alert[] = []

    if (!$connected) {
      newAlerts.push({
        key: 'disconnected',
        severity: 'danger',
        message: 'Miner unreachable'
      })
    }

    if ($stats?.asic_temp_c && $stats.asic_temp_c > 75) {
      newAlerts.push({
        key: 'temp',
        severity: 'warning',
        message: `High temperature: ${$stats.asic_temp_c.toFixed(1)}°C`
      })
    }

    if ($power?.vin_low) {
      newAlerts.push({
        key: 'vin_low',
        severity: 'warning',
        message: $power.vin_mv != null
          ? `Input voltage low: ${($power.vin_mv / 1000).toFixed(2)}V`
          : 'Input voltage low'
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
