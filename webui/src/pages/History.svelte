<script lang="ts">
  import { onMount, onDestroy } from 'svelte'
  import uPlot from 'uplot'
  import 'uplot/dist/uPlot.min.css'
  import { history, type HistorySample } from '../lib/stores'
  import { get } from 'svelte/store'

  type MetricKey =
    | 'total_ghs' | 'hw_err_pct' | 'temp_c' | 'vr_temp_c' | 'board_temp_c'
    | 'pcore_w' | 'vcore_v' | 'efficiency_jth' | 'asic_freq_mhz'
    | 'rpm' | 'fan_duty'
  interface MetricDef {
    key: MetricKey
    label: string
    unit: string
    color: string
    scale: 'a' | 'b'  // dual-axis: a = left (large-magnitude), b = right (small)
  }

  const METRICS: MetricDef[] = [
    { key: 'total_ghs',      label: 'Hashrate',   unit: 'GH/s', color: '#e5ad30', scale: 'a' },
    { key: 'asic_freq_mhz',  label: 'Freq',       unit: 'MHz',  color: '#5c6bc0', scale: 'a' },
    { key: 'rpm',            label: 'Fan RPM',    unit: 'rpm',  color: '#3498db', scale: 'a' },
    { key: 'hw_err_pct',     label: 'HW Error',   unit: '%',    color: '#e74c3c', scale: 'b' },
    { key: 'temp_c',         label: 'ASIC Temp',  unit: '°C',   color: '#c678dd', scale: 'b' },
    { key: 'vr_temp_c',      label: 'VR Temp',    unit: '°C',   color: '#ec407a', scale: 'b' },
    { key: 'board_temp_c',   label: 'Board Temp', unit: '°C',   color: '#e67e22', scale: 'b' },
    { key: 'pcore_w',        label: 'Core Pwr',   unit: 'W',    color: '#4caf50', scale: 'b' },
    { key: 'vcore_v',        label: 'Vcore',      unit: 'V',    color: '#26c6da', scale: 'b' },
    { key: 'efficiency_jth', label: 'J/TH',       unit: 'J/TH', color: '#cddc39', scale: 'b' },
    { key: 'fan_duty',       label: 'Fan Duty',   unit: '%',    color: '#607d8b', scale: 'b' }
  ]

  const WINDOWS = [
    { label: '1m',  seconds: 60 },
    { label: '5m',  seconds: 300 },
    { label: '15m', seconds: 900 },
    { label: '1h',  seconds: 3600 },
    { label: 'All', seconds: 0 }
  ]

  let windowIdx = 2 // default 15m
  const DEFAULT_ON: MetricKey[] = ['total_ghs', 'hw_err_pct']

  let chartEl: HTMLDivElement
  let plot: uPlot | null = null
  let unsub: (() => void) | null = null

  function selectWindow(i: number) {
    windowIdx = i
    rebuild()
  }

  function buildData(samples: HistorySample[]): uPlot.AlignedData {
    const now = Math.floor(Date.now() / 1000)
    const windowSec = WINDOWS[windowIdx].seconds
    const cutoff = windowSec > 0 ? now - windowSec : 0
    const filtered = windowSec > 0 ? samples.filter((s) => s.ts >= cutoff) : samples

    const xs = filtered.map((s) => s.ts)
    const series: number[][] = METRICS.map((m) =>
      filtered.map((s) => (s[m.key] == null ? NaN : (s[m.key] as number)))
    )
    return [xs, ...series] as uPlot.AlignedData
  }

  function buildOpts(width: number): uPlot.Options {
    const series: uPlot.Series[] = [
      {},
      ...METRICS.map((m) => ({
        label: m.label,
        stroke: m.color,
        width: 1.5,
        scale: m.scale,
        show: DEFAULT_ON.includes(m.key),
        value: (_: uPlot, v: number | null) => (v == null ? '—' : v.toFixed(1) + ' ' + m.unit),
        points: { show: false }
      } as uPlot.Series))
    ]

    return {
      width,
      height: 340,
      scales: {
        x: { time: true },
        a: { auto: true },
        b: { auto: true }
      },
      axes: [
        {
          stroke: '#b0b0b0',
          grid: { stroke: '#1a3a52', width: 1 },
          ticks: { stroke: '#1a3a52', width: 1 }
        },
        {
          scale: 'a',
          stroke: '#e5ad30',
          grid: { stroke: '#1a3a52', width: 1 },
          ticks: { stroke: '#1a3a52', width: 1 }
        },
        {
          scale: 'b',
          side: 1,
          stroke: '#b0b0b0',
          grid: { show: false },
          ticks: { stroke: '#1a3a52', width: 1 }
        }
      ],
      series,
      legend: { show: true },
      cursor: {
        drag: { x: true, y: false, setScale: true }
      }
    }
  }

  function rebuild() {
    if (!chartEl) return
    const samples = get(history)
    const data = buildData(samples)

    if (plot) {
      plot.destroy()
      plot = null
    }

    const width = chartEl.clientWidth || 800
    plot = new uPlot(buildOpts(width), data, chartEl)
  }

  function updateData() {
    if (!plot) { rebuild(); return }
    const samples = get(history)
    plot.setData(buildData(samples))
  }

  function onResize() {
    if (plot && chartEl) {
      plot.setSize({ width: chartEl.clientWidth, height: 340 })
    }
  }

  onMount(() => {
    rebuild()
    unsub = history.subscribe(() => updateData())
    window.addEventListener('resize', onResize)
  })

  onDestroy(() => {
    plot?.destroy()
    plot = null
    unsub?.()
    window.removeEventListener('resize', onResize)
  })

  $: count = $history.length
  $: latest = $history[$history.length - 1]
</script>

<div class="page">
  <div class="toolbar">
    <div class="windows">
      {#each WINDOWS as w, i}
        <button
          class="win-btn"
          class:active={windowIdx === i}
          on:click={() => selectWindow(i)}
        >{w.label}</button>
      {/each}
    </div>

  </div>

  {#if count === 0}
    <div class="empty">
      No samples yet. History collects in the background every 5 seconds while the tab is open — come back in a minute or two.
    </div>
  {:else}
    <div class="chart" bind:this={chartEl}></div>
    <div class="hint">
      {count} samples · click a metric in the legend to toggle · drag to zoom · double-click to reset
    </div>
  {/if}
</div>

<style>
  .page {
    display: flex;
    flex-direction: column;
    gap: 16px;
    padding-top: 12px;
  }

  .toolbar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 16px;
    flex-wrap: wrap;
  }

  .windows {
    display: inline-flex;
    border: 1px solid var(--border);
    border-radius: 4px;
    overflow: hidden;
  }

  .win-btn {
    background: transparent;
    border: none;
    border-right: 1px solid var(--border);
    padding: 6px 12px;
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--muted);
    cursor: pointer;
    font-weight: 600;
    transition: background 0.15s, color 0.15s;
  }

  .win-btn:last-child { border-right: none; }

  .win-btn:hover:not(.active) {
    color: var(--text);
    background: var(--input);
  }

  .win-btn.active {
    background: var(--accent);
    color: var(--bg);
  }

  .chart {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 12px;
    min-height: 360px;
  }

  :global(.chart .u-legend) {
    background: transparent;
    color: var(--text);
    font-size: 11px;
  }

  :global(.chart .u-legend .u-value) {
    font-variant-numeric: tabular-nums;
  }

  :global(.chart .u-axis) {
    color: var(--muted);
  }

  .empty {
    padding: 40px 20px;
    text-align: center;
    font-size: 13px;
    color: var(--muted);
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 6px;
  }

  .hint {
    font-size: 11px;
    color: var(--muted);
    text-align: center;
  }
</style>
