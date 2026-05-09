<script lang="ts">
  export let now: number | null = null
  export let m1: number | null = null
  export let m10: number | null = null
  export let h1: number | null = null
  export let expected: number | null = null

  function fmt(v: number | null): string {
    if (v == null || isNaN(v) || v <= 0) return '—'
    return v.toFixed(1)
  }

  $: deltaPct = (now != null && expected != null && expected > 0)
    ? ((now - expected) / expected) * 100
    : null
  $: status = deltaPct == null ? '' : deltaPct > 15 ? 'warn' : ''

  // Sparkline datapoints = 1m/10m/1h rolling efficiency, drawn on top.
  $: sparkPts = [m1 ?? 0, m10 ?? 0, h1 ?? 0]
  $: sparkMax = Math.max(...sparkPts, 0.0001)
  $: sparkMin = Math.min(...sparkPts, 0)
  $: sparkRange = Math.max(sparkMax - sparkMin, 0.0001)
  $: sparkY = sparkPts.map((v) => 44 - ((v - sparkMin) / sparkRange) * 14)
</script>

<div class="tile" data-status={status}>
  <div class="value">
    {fmt(now)}<span class="unit">J/TH</span>{#if deltaPct != null}<span class="delta" class:bad={deltaPct > 5} class:good={deltaPct < -5}>{deltaPct >= 0 ? '+' : ''}{deltaPct.toFixed(0)}%</span>{/if}
  </div>
  <div class="label">Efficiency{#if expected != null && expected > 0}{' '}· exp {fmt(expected)} J/TH{/if}</div>

  <svg class="spark" width="100%" height="46" preserveAspectRatio="none">
    <line x1="9%"      y1={sparkY[0]} x2="16.67%" y2={sparkY[0]} stroke="var(--accent)" stroke-width="1.5" />
    <line x1="16.67%"  y1={sparkY[0]} x2="50%"    y2={sparkY[1]} stroke="var(--accent)" stroke-width="1.5" />
    <line x1="50%"     y1={sparkY[1]} x2="83.33%" y2={sparkY[2]} stroke="var(--accent)" stroke-width="1.5" />
    <line x1="83.33%"  y1={sparkY[2]} x2="91%"    y2={sparkY[2]} stroke="var(--accent)" stroke-width="1.5" />
    <circle cx="16.67%" cy={sparkY[0]} r="2.5" fill="var(--accent)" />
    <circle cx="50%"    cy={sparkY[1]} r="2.5" fill="var(--accent)" />
    <circle cx="83.33%" cy={sparkY[2]} r="2.5" fill="var(--accent)" />
    <text x="16.67%" y="8" text-anchor="middle" class="ptw">1M</text>
    <text x="50%"    y="8" text-anchor="middle" class="ptw">10M</text>
    <text x="83.33%" y="8" text-anchor="middle" class="ptw">1H</text>
    <text x="16.67%" y="20" text-anchor="middle" class="pt">{fmt(m1)}<tspan class="ptu" dx="3">J/TH</tspan></text>
    <text x="50%"    y="20" text-anchor="middle" class="pt">{fmt(m10)}<tspan class="ptu" dx="3">J/TH</tspan></text>
    <text x="83.33%" y="20" text-anchor="middle" class="pt">{fmt(h1)}<tspan class="ptu" dx="3">J/TH</tspan></text>
  </svg>
</div>

<style>
  .tile {
    display: flex;
    flex-direction: column;
    gap: 2px;
    min-width: 0;
    position: relative;
  }
  .spark {
    position: absolute;
    bottom: 100%;
    left: 0;
    right: 0;
    width: 100%;
    height: 46px;
    overflow: visible;
    margin-bottom: 4px;
  }
  .spark .pt {
    font-size: 12px;
    font-weight: 600;
    fill: var(--text);
  }
  .spark .ptw {
    font-size: 9px;
    fill: var(--muted);
  }
  .spark .ptu {
    font-size: 8px;
    font-weight: normal;
    fill: var(--muted);
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
  .delta {
    font-size: 10px;
    color: var(--muted);
    margin-left: 5px;
    font-variant-numeric: tabular-nums;
  }
  .delta.bad { color: var(--warning); }
  .delta.good { color: var(--success, #4caf50); }
  .label {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
  }
  .tile[data-status="warn"] .value {
    color: var(--warning);
  }
</style>
