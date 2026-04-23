<script lang="ts">
  export let label: string
  export let value: number | string | null
  export let unit: string = ''
  export let danger: number | null = null
  export let warn: number | null = null

  $: numeric = typeof value === 'number' ? value : null
  $: status =
    numeric === null || (danger === null && warn === null) ? ''
    : danger !== null && numeric >= danger ? 'danger'
    : warn !== null && numeric >= warn ? 'warn'
    : ''
  $: display = value === null || value === undefined
    ? '—'
    : typeof value === 'number'
      ? (value >= 100 ? value.toFixed(0) : value.toFixed(1))
      : value
</script>

<div class="tile" data-status={status}>
  <div class="value">
    {display}{#if unit}<span class="unit">{unit}</span>{/if}
  </div>
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

  .tile[data-status="warn"] .value {
    color: var(--warning);
  }

  .tile[data-status="danger"] .value {
    color: var(--danger);
  }
</style>
