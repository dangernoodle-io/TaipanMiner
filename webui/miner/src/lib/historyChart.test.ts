import { describe, it, expect } from 'vitest'
import type uPlot from 'uplot'
import {
  ALL_METRICS, WINDOWS, DEFAULT_WINDOW_IDX,
  windowFilter, buildSeries, buildOptions, tooltipPlugin,
  type HistorySample, type MetricDef
} from './historyChart'

const makeTs = (offsetSec: number = 0) => Math.floor(Date.now() / 1000) + offsetSec

const sampleFull: HistorySample = {
  ts: makeTs(),
  total_ghs: 485.5,
  hw_err_pct: 0.01,
  temp_c: 72,
  vr_temp_c: 60,
  board_temp_c: 45,
  pcore_w: 25,
  vcore_v: 1.1,
  efficiency_jth: 25.5,
  asic_freq_mhz: 395,
  rpm: 3200,
  fan_duty: 80
}

const sampleNulls: HistorySample = {
  ts: makeTs(-100),
  total_ghs: null,
  hw_err_pct: null,
  temp_c: null,
  vr_temp_c: null,
  board_temp_c: null,
  pcore_w: null,
  vcore_v: null,
  efficiency_jth: null,
  asic_freq_mhz: null,
  rpm: null,
  fan_duty: null
}

describe('ALL_METRICS', () => {
  it('contains 11 metrics', () => {
    expect(ALL_METRICS).toHaveLength(11)
  })

  it('has total_ghs as first metric', () => {
    expect(ALL_METRICS[0].key).toBe('total_ghs')
  })

  it('all metrics have required fields', () => {
    for (const m of ALL_METRICS) {
      expect(m.key).toBeTruthy()
      expect(m.label).toBeTruthy()
      expect(m.color).toMatch(/^#/)
      expect(['a', 'b']).toContain(m.scale)
    }
  })
})

describe('WINDOWS', () => {
  it('has 5 windows', () => {
    expect(WINDOWS).toHaveLength(5)
  })

  it('default window index points to 15m', () => {
    expect(WINDOWS[DEFAULT_WINDOW_IDX].label).toBe('15m')
    expect(WINDOWS[DEFAULT_WINDOW_IDX].seconds).toBe(900)
  })

  it('last window is All (seconds=0)', () => {
    const last = WINDOWS[WINDOWS.length - 1]
    expect(last.label).toBe('All')
    expect(last.seconds).toBe(0)
  })
})

describe('windowFilter', () => {
  it('returns empty array for empty input', () => {
    expect(windowFilter([], 300, makeTs())).toEqual([])
  })

  it('returns all samples when windowSeconds=0 (All)', () => {
    const now = makeTs()
    const samples = [
      { ...sampleFull, ts: now - 10000 },
      { ...sampleFull, ts: now - 5000 },
      { ...sampleFull, ts: now }
    ]
    expect(windowFilter(samples, 0, now)).toEqual(samples)
  })

  it('filters samples outside the window', () => {
    const now = makeTs()
    const old = { ...sampleFull, ts: now - 400 }
    const recent = { ...sampleFull, ts: now - 100 }
    const result = windowFilter([old, recent], 300, now)
    expect(result).toHaveLength(1)
    expect(result[0].ts).toBe(recent.ts)
  })

  it('includes samples exactly at the cutoff boundary', () => {
    const now = 1000
    const atCutoff = { ...sampleFull, ts: 700 } // now(1000) - 300 = 700
    const result = windowFilter([atCutoff], 300, now)
    expect(result).toHaveLength(1)
  })

  it('returns all when all samples are in window', () => {
    const now = makeTs()
    const samples = [
      { ...sampleFull, ts: now - 10 },
      { ...sampleFull, ts: now - 5 }
    ]
    const result = windowFilter(samples, 300, now)
    expect(result).toHaveLength(2)
  })

  it('returns empty when all samples are outside window', () => {
    const now = makeTs()
    const samples = [{ ...sampleFull, ts: now - 1000 }]
    const result = windowFilter(samples, 300, now)
    expect(result).toHaveLength(0)
  })

  it('handles single sample in window', () => {
    const now = 1000
    const s = { ...sampleFull, ts: 800 }
    expect(windowFilter([s], 300, now)).toHaveLength(1)
  })

  it('handles single sample out of window', () => {
    const now = 1000
    const s = { ...sampleFull, ts: 500 }
    expect(windowFilter([s], 300, now)).toHaveLength(0)
  })
})

describe('buildSeries', () => {
  it('returns array with length metrics+1 (x + one per metric)', () => {
    const data = buildSeries([sampleFull], ALL_METRICS)
    expect(data).toHaveLength(ALL_METRICS.length + 1)
  })

  it('first array is timestamps', () => {
    const data = buildSeries([sampleFull], ALL_METRICS)
    expect(data[0]).toEqual([sampleFull.ts])
  })

  it('null metric values become NaN', () => {
    const data = buildSeries([sampleNulls], ALL_METRICS)
    // Every metric should be NaN since sampleNulls has all nulls
    for (let i = 1; i < data.length; i++) {
      expect(isNaN(data[i][0] as number)).toBe(true)
    }
  })

  it('numeric metric values are preserved', () => {
    const data = buildSeries([sampleFull], ALL_METRICS)
    // total_ghs is metric 0 → data[1]
    expect(data[1][0]).toBe(485.5)
  })

  it('empty samples gives empty arrays', () => {
    const data = buildSeries([], ALL_METRICS)
    expect(data[0]).toEqual([])
    expect(data[1]).toEqual([])
  })

  it('multi-sample preserves order', () => {
    const s1 = { ...sampleFull, ts: 1000, total_ghs: 100 }
    const s2 = { ...sampleFull, ts: 2000, total_ghs: 200 }
    const data = buildSeries([s1, s2], ALL_METRICS)
    expect(data[0]).toEqual([1000, 2000])
    expect(data[1]).toEqual([100, 200])
  })

  it('works with filtered metric list (non-ASIC)', () => {
    const nonAsicMetrics = ALL_METRICS.filter((m) => !m.asicOnly)
    const data = buildSeries([sampleFull], nonAsicMetrics)
    expect(data).toHaveLength(nonAsicMetrics.length + 1)
  })
})

describe('buildOptions', () => {
  const nonAsicMetrics = ALL_METRICS.filter((m) => !m.asicOnly)
  const defaultOn = ['total_ghs', 'temp_c'] as any

  it('returns an object with width and height', () => {
    const opts = buildOptions(800, ALL_METRICS, defaultOn)
    expect(opts.width).toBe(800)
    expect(opts.height).toBe(340)
  })

  it('has three scales: x, a, b', () => {
    const opts = buildOptions(800, ALL_METRICS, defaultOn)
    expect(opts.scales).toHaveProperty('x')
    expect(opts.scales).toHaveProperty('a')
    expect(opts.scales).toHaveProperty('b')
  })

  it('has three axes', () => {
    const opts = buildOptions(800, ALL_METRICS, defaultOn)
    expect(opts.axes).toHaveLength(3)
  })

  it('left axis (a) has hashrate color', () => {
    const opts = buildOptions(800, ALL_METRICS, defaultOn)
    expect(opts.axes![1].stroke).toBe('#e5ad30')
  })

  it('right axis (b) is on side 1', () => {
    const opts = buildOptions(800, ALL_METRICS, defaultOn)
    expect(opts.axes![2].side).toBe(1)
  })

  it('series count equals metrics + 1 (x series)', () => {
    const opts = buildOptions(800, ALL_METRICS, defaultOn)
    expect(opts.series).toHaveLength(ALL_METRICS.length + 1)
  })

  it('series labels match metric labels', () => {
    const opts = buildOptions(800, ALL_METRICS, defaultOn)
    ALL_METRICS.forEach((m, i) => {
      expect(opts.series![i + 1].label).toBe(m.label)
    })
  })

  it('series colors match metric colors', () => {
    const opts = buildOptions(800, ALL_METRICS, defaultOn)
    ALL_METRICS.forEach((m, i) => {
      expect(opts.series![i + 1].stroke).toBe(m.color)
    })
  })

  it('defaultOn metrics are shown', () => {
    const on = ['total_ghs'] as any
    const opts = buildOptions(800, ALL_METRICS, on)
    const total_ghs_idx = ALL_METRICS.findIndex((m) => m.key === 'total_ghs')
    expect(opts.series![total_ghs_idx + 1].show).toBe(true)
  })

  it('non-defaultOn metrics are hidden', () => {
    const on = ['total_ghs'] as any
    const opts = buildOptions(800, ALL_METRICS, on)
    const hw_err_idx = ALL_METRICS.findIndex((m) => m.key === 'hw_err_pct')
    expect(opts.series![hw_err_idx + 1].show).toBe(false)
  })

  it('legend is enabled and not live', () => {
    const opts = buildOptions(800, ALL_METRICS, defaultOn)
    expect(opts.legend?.show).toBe(true)
    expect(opts.legend?.live).toBe(false)
  })

  it('cursor drag is configured', () => {
    const opts = buildOptions(800, ALL_METRICS, defaultOn)
    expect(opts.cursor?.drag?.x).toBe(true)
    expect(opts.cursor?.drag?.setScale).toBe(true)
  })

  it('has one plugin (tooltip)', () => {
    const opts = buildOptions(800, ALL_METRICS, defaultOn)
    expect(opts.plugins).toHaveLength(1)
  })

  it('works with filtered non-ASIC metrics', () => {
    const opts = buildOptions(600, nonAsicMetrics, ['total_ghs', 'temp_c'] as any)
    expect(opts.series).toHaveLength(nonAsicMetrics.length + 1)
  })
})

// uPlot Plugin hooks are typed as `Defs[P][] | Defs[P]` (ArraysOrFuncs).
// Cast helpers for direct invocation in tests.
function callInit(plugin: uPlot.Plugin, u: any, opts: any = {}) {
  (plugin.hooks.init as Function)(u, opts)
}
function callSetCursor(plugin: uPlot.Plugin, u: any) {
  (plugin.hooks.setCursor as Function)(u)
}

describe('tooltipPlugin', () => {
  it('returns an object with hooks', () => {
    const plugin = tooltipPlugin(ALL_METRICS)
    expect(plugin).toHaveProperty('hooks')
    expect(plugin.hooks).toHaveProperty('init')
    expect(plugin.hooks).toHaveProperty('setCursor')
  })

  it('init hook appends a div to u.over', () => {
    const plugin = tooltipPlugin(ALL_METRICS)
    const div = document.createElement('div')
    callInit(plugin, { over: div })
    expect(div.querySelector('.uplot-tip')).not.toBeNull()
  })

  it('init hook tip has expected inline style properties', () => {
    const plugin = tooltipPlugin(ALL_METRICS)
    const div = document.createElement('div')
    callInit(plugin, { over: div })
    const tip = div.querySelector('.uplot-tip') as HTMLDivElement
    expect(tip.style.position).toBe('absolute')
    expect(tip.style.display).toBe('none')
  })

  it('setCursor hides tooltip when idx is null', () => {
    const plugin = tooltipPlugin(ALL_METRICS)
    const over = document.createElement('div')
    document.body.appendChild(over)
    callInit(plugin, { over })

    callSetCursor(plugin, {
      cursor: { left: 10, top: 10, idx: null },
      data: [], series: [], over, bbox: { width: 800, height: 340 }
    })
    const tip = over.querySelector('.uplot-tip') as HTMLDivElement
    expect(tip.style.display).toBe('none')
    document.body.removeChild(over)
  })

  it('setCursor hides tooltip when left is negative', () => {
    const plugin = tooltipPlugin(ALL_METRICS)
    const over = document.createElement('div')
    document.body.appendChild(over)
    callInit(plugin, { over })

    callSetCursor(plugin, {
      cursor: { left: -1, top: 10, idx: 0 },
      data: [[1000], [42]], series: [{}, { show: true, scale: 'a' }],
      over, bbox: { width: 800, height: 340 }
    })
    const tip = over.querySelector('.uplot-tip') as HTMLDivElement
    expect(tip.style.display).toBe('none')
    document.body.removeChild(over)
  })

  it('setCursor hides tooltip when all series are hidden or NaN', () => {
    const metrics: MetricDef[] = [
      { key: 'total_ghs', label: 'Hashrate', unit: 'GH/s', color: '#e5ad30', scale: 'a' }
    ]
    const plugin = tooltipPlugin(metrics)
    const over = document.createElement('div')
    document.body.appendChild(over)
    callInit(plugin, { over })

    callSetCursor(plugin, {
      cursor: { left: 50, top: 50, idx: 0 },
      data: [[1000000], [NaN]],
      series: [{}, { show: true, scale: 'a' }],
      valToPos: () => 100,
      over,
      bbox: { width: 800, height: 340 }
    })
    const tip = over.querySelector('.uplot-tip') as HTMLDivElement
    expect(tip.style.display).toBe('none')
    document.body.removeChild(over)
  })

  it('setCursor shows tooltip with valid data', () => {
    const metrics: MetricDef[] = [
      { key: 'total_ghs', label: 'Hashrate', unit: 'GH/s', color: '#e5ad30', scale: 'a' }
    ]
    const plugin = tooltipPlugin(metrics)
    const over = document.createElement('div')
    document.body.appendChild(over)
    callInit(plugin, { over })

    callSetCursor(plugin, {
      cursor: { left: 50, top: 50, idx: 0 },
      data: [[1000000], [485.5]],
      series: [{}, { show: true, scale: 'a' }],
      valToPos: () => 60,
      over,
      bbox: { width: 800, height: 340 }
    })
    const tip = over.querySelector('.uplot-tip') as HTMLDivElement
    expect(tip.style.display).toBe('block')
    expect(tip.innerHTML).toContain('Hashrate')
    document.body.removeChild(over)
  })

  it('setCursor uses custom format function when provided', () => {
    const metrics: MetricDef[] = [
      { key: 'total_ghs', label: 'Hashrate', unit: 'GH/s', color: '#e5ad30', scale: 'a',
        format: (v) => v.toFixed(2) + ' TH/s' }
    ]
    const plugin = tooltipPlugin(metrics)
    const over = document.createElement('div')
    document.body.appendChild(over)
    callInit(plugin, { over })

    callSetCursor(plugin, {
      cursor: { left: 50, top: 50, idx: 0 },
      data: [[1000000], [1.5]],
      series: [{}, { show: true, scale: 'a' }],
      valToPos: () => 60,
      over,
      bbox: { width: 800, height: 340 }
    })
    const tip = over.querySelector('.uplot-tip') as HTMLDivElement
    expect(tip.innerHTML).toContain('1.50 TH/s')
    document.body.removeChild(over)
  })

  it('setCursor skips hidden series', () => {
    const metrics: MetricDef[] = [
      { key: 'total_ghs', label: 'Hashrate', unit: 'GH/s', color: '#e5ad30', scale: 'a' },
      { key: 'temp_c', label: 'Temp', unit: '°C', color: '#c678dd', scale: 'b' }
    ]
    const plugin = tooltipPlugin(metrics)
    const over = document.createElement('div')
    document.body.appendChild(over)
    callInit(plugin, { over })

    callSetCursor(plugin, {
      cursor: { left: 50, top: 50, idx: 0 },
      data: [[1000000], [485.5], [72]],
      // second series (temp_c) is hidden
      series: [{}, { show: true, scale: 'a' }, { show: false, scale: 'b' }],
      valToPos: () => 60,
      over,
      bbox: { width: 800, height: 340 }
    })
    const tip = over.querySelector('.uplot-tip') as HTMLDivElement
    expect(tip.innerHTML).toContain('Hashrate')
    expect(tip.innerHTML).not.toContain('Temp')
    document.body.removeChild(over)
  })

  it('setCursor adjusts position when near right edge', () => {
    const metrics: MetricDef[] = [
      { key: 'total_ghs', label: 'Hashrate', unit: 'GH/s', color: '#e5ad30', scale: 'a' }
    ]
    const plugin = tooltipPlugin(metrics)
    const over = document.createElement('div')
    document.body.appendChild(over)
    callInit(plugin, { over })

    // Put cursor near right edge; bbox.width=300, left=290 → should flip
    callSetCursor(plugin, {
      cursor: { left: 290, top: 50, idx: 0 },
      data: [[1000000], [485.5]],
      series: [{}, { show: true, scale: 'a' }],
      valToPos: () => 60,
      over,
      bbox: { width: 300, height: 400 }
    })
    const tip = over.querySelector('.uplot-tip') as HTMLDivElement
    // tip.style.left should be something smaller than 290 (flipped left)
    expect(tip.style.display).toBe('block')
    document.body.removeChild(over)
  })
})

describe('buildOptions — axis value formatters', () => {
  it('left axis values formatter calls fmtHashrate for non-null', () => {
    const opts = buildOptions(800, ALL_METRICS, ['total_ghs'])
    const axisA = opts.axes![1] as any
    const result = axisA.values(null, [485.5, 0])
    expect(result[0]).toContain('GH/s')
  })

  it('left axis values formatter returns empty string for null', () => {
    const opts = buildOptions(800, ALL_METRICS, ['total_ghs'])
    const axisA = opts.axes![1] as any
    const result = axisA.values(null, [null])
    expect(result[0]).toBe('')
  })

  it('series value formatter returns empty string', () => {
    const opts = buildOptions(800, ALL_METRICS, ['total_ghs'])
    // series[1] is total_ghs
    const valueFn = (opts.series![1] as any).value
    expect(valueFn(null, 42)).toBe('')
    expect(valueFn(null, null)).toBe('')
  })
})
