<script lang="ts">
  import type { PoolStat } from '../lib/api'
  import { fmtHashGhs, fmtRelative, fmtDiff } from '../lib/fmt'

  interface Props {
    stat: PoolStat
    active: boolean
  }

  let { stat, active = false }: Props = $props()

  const hostPort = $derived(`${stat.host}:${stat.port}`)
</script>

<div class="pool-stats-card" class:active>
  <div class="card-header">
    <div class="host-port">{hostPort}</div>
    {#if stat.blocks_found > 0}
      <div class="blocks-badge">{stat.blocks_found} block{stat.blocks_found !== 1 ? 's' : ''}</div>
    {/if}
  </div>

  <div class="stats-grid">
    <div class="stat-item">
      <div class="stat-label">shares</div>
      <div class="stat-value">{stat.shares}</div>
    </div>

    <div class="stat-item">
      <div class="stat-label">hashes</div>
      <div class="stat-value">{fmtHashGhs(stat.hashes / 1e9)}</div>
    </div>

    <div class="stat-item">
      <div class="stat-label">best diff</div>
      <div class="stat-value mono">{fmtDiff(stat.best_diff)}</div>
    </div>

    {#if stat.last_seen_s > 0}
      <div class="stat-item">
        <div class="stat-label">last seen</div>
        <div class="stat-value">{fmtRelative(stat.last_seen_s)}</div>
      </div>
    {/if}
  </div>
</div>

<style>
  .pool-stats-card {
    padding: 12px;
    border: 1px solid color-mix(in srgb, var(--border) 60%, transparent);
    border-radius: 6px;
    background: color-mix(in srgb, var(--surface) 50%, transparent);
    transition: all 0.15s ease;
  }

  .pool-stats-card.active {
    border-color: var(--accent);
    background: color-mix(in srgb, var(--accent) 8%, transparent);
  }

  .card-header {
    display: flex;
    align-items: center;
    justify-content: space-between;
    margin-bottom: 10px;
    gap: 8px;
  }

  .host-port {
    font-size: 13px;
    font-weight: 600;
    color: var(--text);
    font-family: ui-monospace, Menlo, monospace;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .blocks-badge {
    font-size: 11px;
    font-weight: 700;
    color: #fff;
    background: #1a7a3c;
    padding: 2px 8px;
    border-radius: 999px;
    white-space: nowrap;
    flex-shrink: 0;
  }

  .stats-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(100px, 1fr));
    gap: 10px;
  }

  .stat-item {
    display: flex;
    flex-direction: column;
    gap: 2px;
  }

  .stat-label {
    font-size: 9px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--muted);
  }

  .stat-value {
    font-size: 12px;
    font-weight: 600;
    color: var(--text);
  }

  .stat-value.mono {
    font-family: ui-monospace, Menlo, monospace;
    font-size: 11px;
  }
</style>
