<script lang="ts">
  import { onMount, onDestroy } from 'svelte'
  import uPlot from 'uplot'
  import 'uplot/dist/uPlot.min.css'
  import { history, hasAsic, type HistorySample } from '../lib/stores'
  import { fmtHashGhs } from '../lib/fmt'
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
    format?: (v: number) => string  // custom tooltip/axis formatter (e.g. hashrate unit autoscale)
  }

  // Hashrate is stored internally in GH/s; fmtHashGhs auto-scales the display unit.
  const fmtHashrate = fmtHashGhs

  // ASIC-only metrics are filtered out on boards without ASIC (tdongle).
  interface MetricDefInternal extends MetricDef { asicOnly?: boolean }

  const ALL_METRICS: MetricDefInternal[] = [
    { key: 'total_ghs',      label: 'Hashrate',   unit: 'GH/s', color: '#e5ad30', scale: 'a', format: fmtHashrate },
    { key: 'asic_freq_mhz',  label: 'Freq',       unit: 'MHz',  color: '#5c6bc0', scale: 'a', asicOnly: true },
    { key: 'rpm',            label: 'Fan RPM',    unit: 'rpm',  color: '#3498db', scale: 'a', asicOnly: true },
    { key: 'hw_err_pct',     label: 'HW Error',   unit: '%',    color: '#e74c3c', scale: 'b', asicOnly: true },
    { key: 'temp_c',         label: 'Temp',       unit: '°C',   color: '#c678dd', scale: 'b' },
    { key: 'vr_temp_c',      label: 'VR Temp',    unit: '°C',   color: '#ec407a', scale: 'b', asicOnly: true },
    { key: 'board_temp_c',   label: 'Board Temp', unit: '°C',   color: '#e67e22', scale: 'b', asicOnly: true },
    { key: 'pcore_w',        label: 'Core Pwr',   unit: 'W',    color: '#4caf50', scale: 'b', asicOnly: true },
    { key: 'vcore_v',        label: 'Vcore',      unit: 'V',    color: '#26c6da', scale: 'b', asicOnly: true },
    { key: 'efficiency_jth', label: 'J/TH',       unit: 'J/TH', color: '#cddc39', scale: 'b', asicOnly: true },
    { key: 'fan_duty',       label: 'Fan Duty',   unit: '%',    color: '#607d8b', scale: 'b', asicOnly: true }
  ]

  $: METRICS = $hasAsic ? ALL_METRICS : ALL_METRICS.filter((m) => !m.asicOnly)

  const WINDOWS = [
    { label: '1m',  seconds: 60 },
    { label: '5m',  seconds: 300 },
    { label: '15m', seconds: 900 },
    { label: '1h',  seconds: 3600 },
    { label: 'All', seconds: 0 }
  ]

  let windowIdx = 2 // default 15m
  $: DEFAULT_ON = $hasAsic ? (['total_ghs', 'hw_err_pct'] as MetricKey[]) : (['total_ghs', 'temp_c'] as MetricKey[])
  $: $hasAsic, rebuild() // rebuild when capability changes

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

  function tooltipPlugin(metrics: MetricDef[]): uPlot.Plugin {
    let el: HTMLDivElement
    return {
      hooks: {
        init: (u) => {
          el = document.createElement('div')
          el.className = 'uplot-tip'
          el.style.cssText = 'position:absolute;pointer-events:none;display:none;z-index:2;' +
            'background:rgba(22,33,62,0.96);border:1px solid var(--border);' +
            'border-radius:4px;padding:8px 10px;font-size:11px;color:var(--text);' +
            'white-space:nowrap;box-shadow:0 4px 12px rgba(0,0,0,0.4);' +
            'font-variant-numeric:tabular-nums;backdrop-filter:blur(2px);' +
            'transform:translate(10px,10px)'
          u.over.appendChild(el)
        },
        setCursor: (u) => {
          const { left, top, idx } = u.cursor
          if (idx == null || left == null || left < 0 || top == null || top < 0) {
            el.style.display = 'none'
            return
          }
          const ts = u.data[0][idx] as number
          const date = new Date(ts * 1000)
          const hh = String(date.getHours()).padStart(2, '0')
          const mm = String(date.getMinutes()).padStart(2, '0')
          const ss = String(date.getSeconds()).padStart(2, '0')
          // Collect all visible series' values at this X, plus find the one
          // whose line is nearest the cursor Y so it can be bolded.
          type Row = { m: MetricDef; v: number; dist: number }
          const rows: Row[] = []
          let nearestI = -1
          let nearestDist = Infinity
          metrics.forEach((m, i) => {
            const s = u.series[i + 1]
            if (!s.show) return
            const v = u.data[i + 1][idx] as number | null
            if (v == null || isNaN(v)) return
            const scale = (s as uPlot.Series & { scale?: string }).scale ?? 'y'
            const yPx = u.valToPos(v, scale, true)
            const dist = Math.abs(yPx - top)
            rows.push({ m, v, dist })
            if (dist < nearestDist) { nearestDist = dist; nearestI = rows.length - 1 }
          })
          if (rows.length === 0) {
            el.style.display = 'none'
            return
          }
          let html = `<div style="color:var(--muted);margin-bottom:4px">${hh}:${mm}:${ss}</div>`
          rows.forEach((r, i) => {
            const formatted = r.m.format ? r.m.format(r.v) : r.v.toFixed(1) + ' ' + r.m.unit
            const isNearest = i === nearestI
            const weight = isNearest ? '600' : 'normal'
            const labelColor = isNearest ? 'var(--text)' : 'var(--label)'
            html += `<div style="display:flex;align-items:center;gap:6px;padding:1px 0;font-weight:${weight}">` +
              `<span style="width:8px;height:8px;border-radius:2px;background:${r.m.color};flex-shrink:0"></span>` +
              `<span style="flex:1;color:${labelColor}">${r.m.label}</span>` +
              `<span>${formatted}</span></div>`
          })
          el.innerHTML = html
          el.style.display = 'block'
          // Position, keeping inside chart bounds
          const bbox = u.bbox
          const rect = el.getBoundingClientRect()
          let x = left
          let y = top
          if (x + rect.width + 20 > bbox.width) x = left - rect.width - 20
          if (y + rect.height + 20 > bbox.height) y = top - rect.height - 20
          el.style.left = x + 'px'
          el.style.top = y + 'px'
        }
      }
    }
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
        // Suppress value display in legend — tooltip shows values at cursor.
        value: (_: uPlot, v: number | null) => '',
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
          ticks: { stroke: '#1a3a52', width: 1 },
          values: (_: uPlot, vals: number[]) => vals.map((v) => v != null ? fmtHashrate(v) : '')
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
      legend: { show: true, live: false },
      cursor: {
        drag: { x: true, y: false, setScale: true },
        points: { show: true, size: 6 }
      },
      plugins: [tooltipPlugin(METRICS)]
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
