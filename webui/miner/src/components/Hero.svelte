<script lang="ts">
  import { stats, connected, hasAsic, pool } from '../lib/stores'
  import { fmtDuration, fmtRelative, fmtHashGhs } from '../lib/fmt'

  function fmtDiff(d: number): string {
    if (d >= 1e9) return (d / 1e9).toFixed(2) + 'G'
    if (d >= 1e6) return (d / 1e6).toFixed(2) + 'M'
    if (d >= 1e3) return (d / 1e3).toFixed(2) + 'k'
    return d.toFixed(0)
  }

  $: ghs = $stats?.asic_total_ghs ?? ($stats?.hashrate ? $stats.hashrate / 1e9 : null)
  $: ghs1m = $stats?.asic_total_ghs_1m ?? null
  $: ghs10m = $stats?.asic_total_ghs_10m ?? null
  $: ghs1h = $stats?.asic_total_ghs_1h ?? null
  $: emaGhs = $stats?.asic_hashrate_avg ? $stats.asic_hashrate_avg / 1e9 : ($stats ? $stats.hashrate_avg / 1e9 : null)
  $: expectedGhs = $stats?.expected_ghs ?? null
  $: err = $stats?.asic_hw_error_pct ?? null
  $: err1m = $stats?.asic_hw_error_pct_1m ?? null
  $: err10m = $stats?.asic_hw_error_pct_10m ?? null
  $: err1h = $stats?.asic_hw_error_pct_1h ?? null
  function fmtPct(v: number | null): string { return v == null ? '—' : v.toFixed(2) + '%' }
  function fmtGhsNum(v: number | null): string { return v == null ? '—' : v >= 1000 ? (v/1000).toFixed(2) : v.toFixed(0) }
  function fmtGhsUnit(v: number | null): string { return v == null ? '' : v >= 1000 ? 'TH/s' : 'GH/s' }
  $: accepted = $stats?.session_shares ?? 0
  $: rejected = $stats?.session_rejected ?? 0
  $: acceptRate = accepted + rejected > 0 ? (100 * accepted) / (accepted + rejected) : null
  $: sharesPerHour = $stats && $stats.uptime_s > 60 ? (accepted * 3600) / $stats.uptime_s : null
  $: diffMult = $stats && $pool && $pool.current_difficulty > 0 ? $stats.best_diff / $pool.current_difficulty : null

  const REJECT_REASONS: { key: string; label: string; color: string }[] = [
    { key: 'job_not_found',  label: 'job not found',  color: '#f39c12' },
    { key: 'low_difficulty', label: 'low diff',       color: '#e74c3c' },
    { key: 'duplicate',      label: 'duplicate',      color: '#3498db' },
    { key: 'stale_prevhash', label: 'stale prevhash', color: '#9b59b6' },
    { key: 'other',          label: 'other',          color: '#7f8c8d' }
  ]
  $: rejectSegments = (() => {
    const r = $stats?.rejected
    if (!r) return [] as { key: string; label: string; color: string; count: number }[]
    return REJECT_REASONS
      .map((d) => ({ ...d, count: (r as any)[d.key] as number }))
      .filter((p) => p.count > 0)
  })()
  $: rejectTotal = rejectSegments.reduce((a, b) => a + b.count, 0)

  $: sparkPts = [ghs1m ?? 0, ghs10m ?? 0, ghs1h ?? 0]
  $: sparkMax = Math.max(...sparkPts, 0.0001)
  $: sparkMin = Math.min(...sparkPts, 0)
  $: sparkRange = Math.max(sparkMax - sparkMin, 0.0001)
  $: sparkY = sparkPts.map((v) => 14 - ((v - sparkMin) / sparkRange) * 12)
</script>

{#if $stats}
  <div class="hero">
    <div class="top">
      <div class="primary">
        <div class="hashrate">
          <span class="dot" class:connected={$connected} class:disconnected={!$connected}></span>
          <div class="value">{fmtHashGhs(ghs)}</div>
        </div>
        <div class="sub-metrics">
          <div class="kv"><span class="k">Avg</span><span class="v">{fmtHashGhs(emaGhs)}</span></div>
          <div class="kv"><span class="k">Expected</span><span class="v">{fmtHashGhs(expectedGhs)}</span></div>
          {#if $hasAsic}
            <div class="kv"><span class="k">err</span><span class="v" class:bad={err != null && err > 1}>{fmtPct(err)}</span></div>
          {:else}
            <div class="kv"><span class="k">Die Temp</span><span class="v" class:bad={$stats?.temp_c != null && $stats.temp_c > 75}>{$stats?.temp_c != null ? $stats.temp_c.toFixed(1) + '°C' : '—'}</span></div>
          {/if}
        </div>
      </div>

      <div class="rolling">
        <div class="rolling-head">
          <span>1m</span><span>10m</span><span>1h</span>
        </div>
        <div class="rolling-row">
          <div class="cell"><span class="n">{fmtGhsNum(ghs1m)}</span><span class="u">{fmtGhsUnit(ghs1m)}</span></div>
          <div class="cell"><span class="n">{fmtGhsNum(ghs10m)}</span><span class="u">{fmtGhsUnit(ghs10m)}</span></div>
          <div class="cell"><span class="n">{fmtGhsNum(ghs1h)}</span><span class="u">{fmtGhsUnit(ghs1h)}</span></div>
        </div>
        <div class="rolling-row err">
          <div class="cell"><span class="p" class:bad={err1m != null && err1m > 1}>{fmtPct(err1m)}</span></div>
          <div class="cell"><span class="p" class:bad={err10m != null && err10m > 1}>{fmtPct(err10m)}</span></div>
          <div class="cell"><span class="p" class:bad={err1h != null && err1h > 1}>{fmtPct(err1h)}</span></div>
        </div>
        <svg class="rolling-spark" width="100%" height="16" preserveAspectRatio="none">
          <line x1="9%"      y1={sparkY[0]} x2="16.67%" y2={sparkY[0]} stroke="var(--accent)" stroke-width="1.5" />
          <line x1="16.67%"  y1={sparkY[0]} x2="50%"    y2={sparkY[1]} stroke="var(--accent)" stroke-width="1.5" />
          <line x1="50%"     y1={sparkY[1]} x2="83.33%" y2={sparkY[2]} stroke="var(--accent)" stroke-width="1.5" />
          <line x1="83.33%"  y1={sparkY[2]} x2="91%"    y2={sparkY[2]} stroke="var(--accent)" stroke-width="1.5" />
          <circle cx="16.67%" cy={sparkY[0]} r="2" fill="var(--accent)" />
          <circle cx="50%"    cy={sparkY[1]} r="2" fill="var(--accent)" />
          <circle cx="83.33%" cy={sparkY[2]} r="2" fill="var(--accent)" />
        </svg>
      </div>
    </div>

    <div class="row">
      <div class="stat">
        <div class="sv">{accepted}<span class="sep">/</span><span class="rej">{rejected}</span></div>
        <div class="sl">shares {acceptRate !== null ? `(${acceptRate.toFixed(1)}%)` : ''}</div>
      </div>
      <div class="stat">
        <div class="sv">{$stats.lifetime_shares.toLocaleString()}</div>
        <div class="sl">lifetime</div>
      </div>
      <div class="stat">
        <div class="sv">{sharesPerHour !== null ? sharesPerHour.toFixed(sharesPerHour >= 10 ? 0 : 1) : '—'}</div>
        <div class="sl">shares/hr</div>
      </div>
      <div class="stat">
        <div class="sv">{fmtRelative($stats.last_share_ago_s)}</div>
        <div class="sl">last share</div>
      </div>
      <div class="stat">
        <div class="sv">{fmtDiff($stats.best_diff)}{#if diffMult}<span class="mult"> · {diffMult.toFixed(0)}×</span>{/if}</div>
        <div class="sl">best diff</div>
      </div>
      <div class="stat">
        <div class="sv">{fmtDuration($stats.uptime_s)}</div>
        <div class="sl">uptime</div>
      </div>
    </div>
  </div>
  {#if rejectSegments.length > 0}
    <div class="reject-strip" role="region" aria-label="Rejected share breakdown">
      <span class="rs-title">Rejected</span>
      {#each rejectSegments as s (s.key)}
        <span class="rs-item">
          <span class="rs-dot" style="background: {s.color}"></span>
          <span class="rs-label">{s.label}</span>
          <span class="rs-count">{s.count}</span>
        </span>
      {/each}
    </div>
  {/if}
{/if}

<style>
  .hero {
    padding: 18px 20px;
  }

  .top {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 20px;
    margin-bottom: 16px;
    padding-bottom: 14px;
    border-bottom: 1px dashed var(--border);
    flex-wrap: wrap;
  }

  @media (max-width: 720px) {
    .top {
      flex-direction: column;
      align-items: stretch;
    }
    .rolling {
      width: 100%;
    }
  }

  .primary {
    display: flex;
    align-items: center;
    justify-content: flex-start;
    gap: 24px;
    flex: 1;
  }

  .hashrate {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  .dot {
    display: inline-block;
    width: 10px;
    height: 10px;
    border-radius: 50%;
    flex-shrink: 0;
  }

  .dot.connected {
    background: var(--success);
    animation: pulse 2s ease-in-out infinite;
  }

  .dot.disconnected {
    background: var(--danger);
  }

  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.4; }
  }

  .sub-metrics {
    display: flex;
    flex-direction: column;
    gap: 4px;
    text-align: left;
  }

  .kv .v.bad { color: var(--warning); }

  .rolling {
    display: grid;
    gap: 3px;
    min-width: 0;
    flex-shrink: 1;
  }

  .rolling-spark {
    width: 100%;
    height: 16px;
    overflow: visible;
  }

  .rolling-head {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    font-size: 9px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--muted);
    text-align: center;
  }

  .rolling-row {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    text-align: center;
  }

  .rolling-row .cell {
    display: inline-flex;
    justify-content: center;
    align-items: baseline;
    gap: 3px;
  }

  .rolling-row .cell .n {
    font-size: 15px;
    font-weight: 600;
    color: var(--text);
    font-variant-numeric: tabular-nums;
  }

  .rolling-row .cell .u {
    font-size: 9px;
    color: var(--muted);
    text-transform: uppercase;
  }

  .rolling-row.err .cell .p {
    font-size: 11px;
    color: var(--label);
    font-variant-numeric: tabular-nums;
  }

  .rolling-row.err .cell .p.bad {
    color: var(--warning);
  }

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


  .value {
    font-size: 34px;
    font-weight: 600;
    color: var(--accent);
    line-height: 1;
    font-variant-numeric: tabular-nums;
  }

  .kv {
    font-size: 11px;
    color: var(--muted);
  }

  .kv .k {
    text-transform: uppercase;
    letter-spacing: 0.5px;
    margin-right: 4px;
  }

  .kv .v {
    color: var(--label);
    font-variant-numeric: tabular-nums;
  }

  .sparkline {
    display: grid;
    grid-template-rows: auto auto auto;
    justify-items: stretch;
    text-align: center;
    min-width: 110px;
  }

  .spark-labels, .spark-ticks {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    font-size: 10px;
    color: var(--muted);
    font-variant-numeric: tabular-nums;
  }

  .spark-ticks {
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--muted);
  }

  .row {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(110px, 1fr));
    gap: 12px 18px;
  }

  .stat .sv {
    font-size: 18px;
    font-weight: 600;
    color: var(--text);
    font-variant-numeric: tabular-nums;
    line-height: 1.1;
  }

  .stat .sl {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    margin-top: 2px;
  }

  .sep {
    color: var(--muted);
    margin: 0 3px;
  }

  .rej {
    color: var(--warning);
  }

  .mult {
    color: var(--muted);
    font-weight: normal;
    font-size: 13px;
  }
</style>
