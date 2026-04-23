<script lang="ts">
  import { stats, connected } from '../lib/stores'
  import Sparkline from './Sparkline.svelte'

  function fmtHashGhs(ghs: number | null): string {
    if (ghs === null) return '—'
    if (ghs >= 1000) return (ghs / 1000).toFixed(2) + ' TH/s'
    if (ghs >= 1) return ghs.toFixed(1) + ' GH/s'
    return (ghs * 1000).toFixed(1) + ' MH/s'
  }

  function fmtUptime(s: number): string {
    if (s < 60) return `${Math.floor(s)}s`
    if (s < 3600) return `${Math.floor(s / 60)}m ${Math.floor(s % 60)}s`
    const h = Math.floor(s / 3600)
    const m = Math.floor((s % 3600) / 60)
    if (h < 24) return `${h}h ${m}m`
    const d = Math.floor(h / 24)
    return `${d}d ${h % 24}h`
  }

  function fmtLastShare(s: number | null): string {
    if (s === null || s < 0) return '—'
    if (s < 60) return `${Math.floor(s)}s ago`
    if (s < 3600) return `${Math.floor(s / 60)}m ago`
    return `${Math.floor(s / 3600)}h ago`
  }

  function fmtDiff(d: number): string {
    if (d >= 1e9) return (d / 1e9).toFixed(2) + 'G'
    if (d >= 1e6) return (d / 1e6).toFixed(2) + 'M'
    if (d >= 1e3) return (d / 1e3).toFixed(2) + 'k'
    return d.toFixed(0)
  }

  $: ghs = $stats?.asic_total_ghs ?? ($stats ? $stats.hw_hashrate / 1e9 : null)
  $: ghs1m = $stats?.asic_total_ghs_1m ?? null
  $: ghs10m = $stats?.asic_total_ghs_10m ?? null
  $: ghs1h = $stats?.asic_total_ghs_1h ?? null
  $: emaGhs = $stats?.asic_hashrate_avg ? $stats.asic_hashrate_avg / 1e9 : ($stats ? $stats.hashrate_avg / 1e9 : null)
  $: expectedGhs =
    $stats?.asic_freq_configured_mhz && $stats?.asic_small_cores && $stats?.asic_count
      ? ($stats.asic_freq_configured_mhz * $stats.asic_small_cores * $stats.asic_count) / 1000
      : null
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
  $: diffMult = $stats && $stats.pool_difficulty > 0 ? $stats.best_diff / $stats.pool_difficulty : null
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
          <div class="kv"><span class="k">err</span><span class="v" class:bad={err != null && err > 1}>{fmtPct(err)}</span></div>
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
        <Sparkline points={[ghs1m ?? 0, ghs10m ?? 0, ghs1h ?? 0]} width={180} height={16} />
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
        <div class="sv">{fmtLastShare($stats.last_share_ago_s)}</div>
        <div class="sl">last share</div>
      </div>
      <div class="stat">
        <div class="sv">{fmtDiff($stats.best_diff)}{#if diffMult}<span class="mult"> · {diffMult.toFixed(0)}×</span>{/if}</div>
        <div class="sl">best diff</div>
      </div>
      <div class="stat">
        <div class="sv">{fmtUptime($stats.uptime_s)}</div>
        <div class="sl">uptime</div>
      </div>
    </div>
  </div>
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
    min-width: 190px;
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
