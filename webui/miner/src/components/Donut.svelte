<script lang="ts">
  import Tooltip from './Tooltip.svelte'

  interface Props {
    used: number | null | undefined
    total: number | null | undefined
    label: string
    format?: 'bytes' | 'percent'
    size?: number
    hint?: string
  }
  let { used, total, label, format = 'bytes', size = 120, hint }: Props = $props()

  const ratio = $derived(used != null && total != null && total > 0 ? Math.min(used / total, 1) : 0)
  const pct = $derived(ratio * 100)
  const strokeColor = $derived(
    pct > 90 ? 'var(--danger)' :
    pct > 75 ? 'var(--warning)' :
    'var(--accent)'
  )

  const radius = 42
  const circumference = 2 * Math.PI * radius

  const dashOffset = $derived(circumference * (1 - ratio))

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
  <div class="label">{label}{#if hint}<Tooltip text={hint} icon placement="top" />{/if}</div>
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
    /* scale text with the donut so it doesn't squish at smaller sizes */
    font-size: calc(var(--size) * 0.175);
    font-weight: 600;
    color: var(--text);
    line-height: 1;
    font-variant-numeric: tabular-nums;
  }

  .sub {
    font-size: calc(var(--size) * 0.078);
    color: var(--label);
    font-variant-numeric: tabular-nums;
  }

  .label {
    display: inline-flex;
    align-items: center;
    gap: 4px;
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--muted);
    font-weight: 600;
  }
</style>
