<script lang="ts">
  import type { PoolStat } from '../lib/api'
  import { fmtHashGhs, fmtDiff, fmtRelativeFromUnixTs } from '../lib/fmt'

  interface Props {
    stat: PoolStat
    active: boolean
  }

  let { stat, active = false }: Props = $props()

  const hostPort = $derived(`${stat.host}:${stat.port}`)
  const blockRel = $derived(fmtRelativeFromUnixTs(stat.last_block_ts))
  const bestDiffRel = $derived(fmtRelativeFromUnixTs(stat.best_diff_ts))
</script>

<div class="pool-stats-card" class:flat={active}>
  {#if !active}
    <div class="host-port">{hostPort}</div>
  {/if}

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
      <div class="stat-sub">{bestDiffRel}</div>
    </div>

    <div class="stat-item">
      <div class="stat-label">blocks</div>
      <div class="stat-value" class:blocks-found={stat.blocks_found > 0}>
        {stat.blocks_found}
      </div>
      <div class="stat-sub">{blockRel}</div>
    </div>
  </div>
</div>

<style>
  .pool-stats-card {
    padding: 12px;
    border: 1px solid color-mix(in srgb, var(--border) 60%, transparent);
    border-radius: 6px;
    background: color-mix(in srgb, var(--surface) 50%, transparent);
  }

  /* When this card sits inside the parent Active section, drop the nested
   * box — it would otherwise be a box-in-a-box. */
  .pool-stats-card.flat {
    padding: 0;
    border: none;
    background: none;
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
    margin-bottom: 10px;
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
    font-variant-numeric: tabular-nums;
  }

  .stat-value.mono {
    font-family: ui-monospace, Menlo, monospace;
    font-size: 11px;
  }

  .stat-value.blocks-found {
    color: var(--accent, #4ade80);
  }

  .stat-sub {
    font-size: 10px;
    color: var(--muted);
    font-variant-numeric: tabular-nums;
  }
</style>
