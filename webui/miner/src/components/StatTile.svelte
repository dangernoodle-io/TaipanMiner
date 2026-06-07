<script lang="ts">
  interface Props {
    label: string
    value: number | string | null
    unit?: string
    danger?: number | null
    warn?: number | null
    flag?: 'warn' | 'danger' | null
    decimals?: number | null
    progress?: number | null  // 0-100, draws a subtle fill bar under .value
  }
  let { label, value, unit = '', danger = null, warn = null, flag = null, decimals = null, progress = null }: Props = $props()

  const numeric = $derived(typeof value === 'number' ? value : null)
  const status = $derived(
    flag !== null ? flag
    : numeric === null || (danger === null && warn === null) ? ''
    : danger !== null && numeric >= danger ? 'danger'
    : warn !== null && numeric >= warn ? 'warn'
    : ''
  )
  const display = $derived(
    value === null || value === undefined
      ? '—'
      : typeof value === 'number'
        ? (decimals !== null ? value.toFixed(decimals) : value >= 100 ? value.toFixed(0) : value.toFixed(1))
        : value
  )
</script>

<div class="tile" data-status={status}>
  <div class="value">
    {display}{#if unit}<span class="unit">{unit}</span>{/if}
  </div>
  {#if progress != null}
    <div class="bar"><div class="bar-fill" style="width: {Math.max(0, Math.min(100, progress))}%"></div></div>
  {/if}
  <div class="label">{label}</div>
</div>

<style>
  .tile {
    display: flex;
    flex-direction: column;
    gap: 2px;
    min-width: 0;
  }

  .value {
    font-size: 22px;
    font-weight: 600;
    color: var(--text);
    line-height: 1.1;
    font-variant-numeric: tabular-nums;
  }

  .unit {
    font-size: 11px;
    color: var(--muted);
    margin-left: 3px;
    font-weight: normal;
  }

  .label {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
  }

  .bar {
    height: 3px;
    background: var(--border);
    border-radius: 2px;
    overflow: hidden;
    margin: 1px 0 2px;
  }
  .bar-fill {
    height: 100%;
    background: var(--accent);
    transition: width 0.5s ease;
  }
  .tile[data-status="warn"] .bar-fill { background: var(--warning); }
  .tile[data-status="danger"] .bar-fill { background: var(--danger); }

  .tile[data-status="warn"] .value {
    color: var(--warning);
  }

  .tile[data-status="danger"] .value {
    color: var(--danger);
  }
</style>
