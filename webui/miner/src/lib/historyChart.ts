import type uPlot from 'uplot'
import type { HistorySample } from './stores'
import { fmtHashGhs } from './fmt'

export type { HistorySample }

export type MetricKey =
  | 'total_ghs' | 'hw_err_pct' | 'temp_c' | 'vr_temp_c' | 'board_temp_c'
  | 'pcore_w' | 'vcore_v' | 'efficiency_jth' | 'asic_freq_mhz'
  | 'rpm' | 'fan_duty'

export interface MetricDef {
  key: MetricKey
  label: string
  unit: string
  color: string
  scale: 'a' | 'b'
  format?: (v: number) => string
  asicOnly?: boolean
}

// Hashrate is stored internally in GH/s; fmtHashGhs auto-scales the display unit.
const fmtHashrate = fmtHashGhs

export const ALL_METRICS: MetricDef[] = [
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

export const WINDOWS = [
  { label: '1m',  seconds: 60 },
  { label: '5m',  seconds: 300 },
  { label: '15m', seconds: 900 },
  { label: '1h',  seconds: 3600 },
  { label: 'All', seconds: 0 }
]

export const DEFAULT_WINDOW_IDX = 2 // 15m

/**
 * Filter samples to the window covering the last `windowSeconds` seconds.
 * Pass windowSeconds=0 for "All" (no filter).
 */
export function windowFilter(samples: HistorySample[], windowSeconds: number, now: number): HistorySample[] {
  if (windowSeconds <= 0) return samples
  const cutoff = now - windowSeconds
  return samples.filter((s) => s.ts >= cutoff)
}

/**
 * Convert filtered samples into uPlot AlignedData (x[], y1[], y2[], …).
 * Null/undefined values become NaN (uPlot convention).
 */
export function buildSeries(samples: HistorySample[], metrics: MetricDef[]): uPlot.AlignedData {
  const xs = samples.map((s) => s.ts)
  const series: number[][] = metrics.map((m) =>
    samples.map((s) => (s[m.key] == null ? NaN : (s[m.key] as number)))
  )
  return [xs, ...series] as uPlot.AlignedData
}

/**
 * Build the tooltip plugin. Identical to what History.svelte had inline.
 */
export function tooltipPlugin(metrics: MetricDef[]): uPlot.Plugin {
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

/**
 * Build the full uPlot Options object.
 * metrics: the active (possibly filtered) metric list passed at chart build time.
 * defaultOn: keys that should be shown by default.
 */
export function buildOptions(
  width: number,
  metrics: MetricDef[],
  defaultOn: MetricKey[]
): uPlot.Options {
  const series: uPlot.Series[] = [
    {},
    ...metrics.map((m) => ({
      label: m.label,
      stroke: m.color,
      width: 1.5,
      scale: m.scale,
      show: defaultOn.includes(m.key),
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
        size: 72,
        gap: 8,
        stroke: '#e5ad30',
        grid: { stroke: '#1a3a52', width: 1 },
        ticks: { stroke: '#1a3a52', width: 1 },
        values: (_: uPlot, vals: number[]) => vals.map((v) => v != null ? fmtHashrate(v) : '')
      },
      {
        scale: 'b',
        side: 1,
        size: 72,
        gap: 8,
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
    plugins: [tooltipPlugin(metrics)]
  }
}
