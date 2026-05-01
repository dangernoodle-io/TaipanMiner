<script lang="ts">
  import type { RejectedBreakdown } from '../lib/api'

  export let rejected: RejectedBreakdown | null | undefined = undefined

  const REJECT_REASONS: { key: string; label: string; color: string }[] = [
    { key: 'job_not_found',  label: 'job not found',  color: '#f39c12' },
    { key: 'low_difficulty', label: 'low diff',       color: '#e74c3c' },
    { key: 'duplicate',      label: 'duplicate',      color: '#3498db' },
    { key: 'stale_prevhash', label: 'stale prevhash', color: '#9b59b6' },
    { key: 'other',          label: 'other',          color: '#7f8c8d' }
  ]

  $: segments = (() => {
    const r = rejected
    if (!r) return [] as { key: string; label: string; color: string; count: number }[]
    return REJECT_REASONS
      .map((d) => ({ ...d, count: (r as unknown as Record<string, number>)[d.key] ?? 0 }))
      .filter((p) => p.count > 0)
  })()
</script>

{#if segments.length > 0}
  <div class="reject-strip" role="region" aria-label="Rejected share breakdown">
    <span class="rs-title">Rejected</span>
    {#each segments as s (s.key)}
      <span class="rs-item">
        <span class="rs-dot" style="background: {s.color}"></span>
        <span class="rs-label">{s.label}</span>
        <span class="rs-count">{s.count}</span>
      </span>
    {/each}
  </div>
{/if}

<style>
  .reject-strip {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    gap: 8px 14px;
    padding: 8px 20px;
    border-bottom-left-radius: 7px;
    border-bottom-right-radius: 7px;
    font-size: 11px;
    font-variant-numeric: tabular-nums;
  }

  .rs-title {
    color: var(--warning);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-weight: 600;
    font-size: 10px;
  }

  .rs-item {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    color: var(--label);
  }

  .rs-dot {
    width: 8px;
    height: 8px;
    border-radius: 2px;
    flex-shrink: 0;
  }

  .rs-label {
    text-transform: uppercase;
    letter-spacing: 0.4px;
    font-size: 10px;
  }

  .rs-count {
    color: var(--text);
    font-weight: 600;
  }
</style>
