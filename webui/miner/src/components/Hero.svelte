<script lang="ts">
  import { stats, connected, hasAsic, pool } from '../lib/stores'
  import { fmtDuration, fmtRelative, fmtHashGhs, fmtDiff, fmtPct } from '../lib/fmt'
  import RollingRates from './RollingRates.svelte'
  import RejectStrip from './RejectStrip.svelte'

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
  $: accepted = $stats?.session_shares ?? 0
  $: rejected = $stats?.session_rejected ?? 0
  $: acceptRate = accepted + rejected > 0 ? (100 * accepted) / (accepted + rejected) : null
  $: sharesPerHour = $stats && $stats.uptime_s > 60 ? (accepted * 3600) / $stats.uptime_s : null
  $: diffMult = $stats && $pool && $pool.current_difficulty > 0 ? $stats.best_diff / $pool.current_difficulty : null
</script>

{#if $stats}
  <div class="hero">
    <div class="top">
      <div class="primary">
        <div class="hashrate">
          <span class="conn-dot" class:connected={$connected} class:disconnected={!$connected}></span>
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

      <RollingRates {ghs1m} {ghs10m} {ghs1h} {err1m} {err10m} {err1h} />
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
        <div class="sv">{fmtDiff($stats.best_diff)}</div>
        <div class="sl">best diff {#if diffMult}({diffMult.toFixed(0)}×){/if}</div>
      </div>
      <div class="stat">
        <div class="sv">{fmtDuration($stats.uptime_s)}</div>
        <div class="sl">uptime</div>
      </div>
    </div>
  </div>
  <RejectStrip rejected={$stats?.rejected} />
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
    .top :global(.rolling) {
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

  /* Hero uses a slightly larger dot than the shared 8px default */
  .conn-dot {
    width: 10px;
    height: 10px;
  }

  .sub-metrics {
    display: flex;
    flex-direction: column;
    gap: 4px;
    text-align: left;
  }

  .kv .v.bad { color: var(--warning); }

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

  .row {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    gap: 18px;
    flex-wrap: wrap;
  }

  .row .stat { text-align: left; }
  .row .stat:last-child { text-align: right; }

  @media (max-width: 720px) {
    .row {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(110px, 1fr));
      gap: 12px 18px;
    }
    .row .stat,
    .row .stat:last-child { text-align: left; }
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
</style>
