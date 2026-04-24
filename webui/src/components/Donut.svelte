<script lang="ts">
  export let used: number | null | undefined
  export let total: number | null | undefined
  export let label: string
  export let format: 'bytes' | 'percent' = 'bytes'
  export let size = 120

  $: ratio = used != null && total != null && total > 0 ? Math.min(used / total, 1) : 0
  $: pct = ratio * 100
  $: strokeColor =
    pct > 90 ? 'var(--danger)' :
    pct > 75 ? 'var(--warning)' :
    'var(--accent)'

  const radius = 42
  const circumference = 2 * Math.PI * radius

  $: dashOffset = circumference * (1 - ratio)

  function fmtBytes(b: number | null | undefined): string {
    if (b == null) return '—'
    if (b < 1024) return `${b} B`
    if (b < 1024 * 1024) return `${(b / 1024).toFixed(0)} KB`
    return `${(b / 1024 / 1024).toFixed(1)} MB`
  }
</script>

<div class="donut" style="--size: {size}px">
  <svg viewBox="0 0 100 100" width={size} height={size}>
    <circle cx="50" cy="50" r={radius} fill="none" stroke="var(--border)" stroke-width="8" />
    <circle
      cx="50" cy="50" r={radius}
      fill="none"
      stroke={strokeColor}
      stroke-width="8"
      stroke-linecap="round"
      stroke-dasharray={circumference}
      stroke-dashoffset={dashOffset}
      transform="rotate(-90 50 50)"
      style="transition: stroke-dashoffset 0.5s ease, stroke 0.3s"
    />
  </svg>
  <div class="inner">
    <div class="pct">{used != null && total != null ? pct.toFixed(0) + '%' : '—'}</div>
    <div class="sub">
      {#if format === 'bytes'}
        {fmtBytes(used)} / {fmtBytes(total)}
      {:else}
        {used ?? '—'} / {total ?? '—'}
      {/if}
    </div>
  </div>
  <div class="label">{label}</div>
</div>

<style>
  .donut {
    position: relative;
    display: flex;
    flex-direction: column;
    align-items: center;
    gap: 6px;
  }

  svg {
    display: block;
  }

  .inner {
    position: absolute;
    top: 0;
    left: 0;
    width: var(--size);
    height: var(--size);
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 2px;
  }

  .pct {
    font-size: 20px;
    font-weight: 600;
    color: var(--text);
    line-height: 1;
    font-variant-numeric: tabular-nums;
  }

  .sub {
    font-size: 9px;
    color: var(--muted);
    font-variant-numeric: tabular-nums;
  }

  .label {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    font-weight: 600;
  }
</style>
