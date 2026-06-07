<script lang="ts">
  import { fmtGhsNum, fmtGhsUnit, fmtPct } from '../lib/fmt'

  interface Props {
    ghs1m?: number | null
    ghs10m?: number | null
    ghs1h?: number | null
    err1m?: number | null
    err10m?: number | null
    err1h?: number | null
    showErr?: boolean
    // Generic mode: pass `values` (+ optional unit/decimals) to render an
    // arbitrary 1m/10m/1h metric instead of hashrate (e.g. efficiency).
    // `showSpark={false}` renders numbers only (caller draws its own sparkline).
    values?: (number | null)[] | null
    unit?: string
    decimals?: number
    showSpark?: boolean
    sparkTop?: boolean
  }
  let {
    ghs1m = null, ghs10m = null, ghs1h = null,
    err1m = null, err10m = null, err1h = null,
    showErr = true, values = null, unit = '', decimals = 1,
    showSpark = true, sparkTop = false
  }: Props = $props()

  const generic = $derived(values != null)
  const v1 = $derived(generic ? values![0] ?? null : ghs1m)
  const v2 = $derived(generic ? values![1] ?? null : ghs10m)
  const v3 = $derived(generic ? values![2] ?? null : ghs1h)

  function fmtVal(v: number | null): string {
    if (v == null || isNaN(v) || v <= 0) return '—'
    return v.toFixed(decimals)
  }

  const sparkPts = $derived([v1 ?? 0, v2 ?? 0, v3 ?? 0])
  const sparkMax = $derived(Math.max(...sparkPts, 0.0001))
  const sparkMin = $derived(Math.min(...sparkPts, 0))
  const sparkRange = $derived(Math.max(sparkMax - sparkMin, 0.0001))
  const sparkY = $derived(sparkPts.map((v) => 14 - ((v - sparkMin) / sparkRange) * 12))
</script>

<div class="rolling">
  <div class="rolling-head">
    <span>1m</span><span>10m</span><span>1h</span>
  </div>
  <div class="rolling-row">
    {#if generic}
      <div class="cell"><span class="n">{fmtVal(v1)}</span>{#if unit}<span class="u">{unit}</span>{/if}</div>
      <div class="cell"><span class="n">{fmtVal(v2)}</span>{#if unit}<span class="u">{unit}</span>{/if}</div>
      <div class="cell"><span class="n">{fmtVal(v3)}</span>{#if unit}<span class="u">{unit}</span>{/if}</div>
    {:else}
      <div class="cell"><span class="n">{fmtGhsNum(ghs1m)}</span><span class="u">{fmtGhsUnit(ghs1m)}</span></div>
      <div class="cell"><span class="n">{fmtGhsNum(ghs10m)}</span><span class="u">{fmtGhsUnit(ghs10m)}</span></div>
      <div class="cell"><span class="n">{fmtGhsNum(ghs1h)}</span><span class="u">{fmtGhsUnit(ghs1h)}</span></div>
    {/if}
  </div>
  {#if showErr && !generic}
    <div class="rolling-row err">
      <div class="cell"><span class="p" class:bad={err1m != null && err1m > 1}>{fmtPct(err1m)}</span></div>
      <div class="cell"><span class="p" class:bad={err10m != null && err10m > 1}>{fmtPct(err10m)}</span></div>
      <div class="cell"><span class="p" class:bad={err1h != null && err1h > 1}>{fmtPct(err1h)}</span></div>
    </div>
  {/if}
  {#if showSpark}
    <svg class="rolling-spark" class:spark-top={sparkTop} width="100%" height="16" preserveAspectRatio="none">
      <line x1="9%"      y1={sparkY[0]} x2="16.67%" y2={sparkY[0]} stroke="var(--accent)" stroke-width="1.5" />
      <line x1="16.67%"  y1={sparkY[0]} x2="50%"    y2={sparkY[1]} stroke="var(--accent)" stroke-width="1.5" />
      <line x1="50%"     y1={sparkY[1]} x2="83.33%" y2={sparkY[2]} stroke="var(--accent)" stroke-width="1.5" />
      <line x1="83.33%"  y1={sparkY[2]} x2="91%"    y2={sparkY[2]} stroke="var(--accent)" stroke-width="1.5" />
      <circle cx="16.67%" cy={sparkY[0]} r="2" fill="var(--accent)" />
      <circle cx="50%"    cy={sparkY[1]} r="2" fill="var(--accent)" />
      <circle cx="83.33%" cy={sparkY[2]} r="2" fill="var(--accent)" />
    </svg>
  {/if}
</div>

<style>
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
  .rolling-spark.spark-top { order: -1; }

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
</style>
