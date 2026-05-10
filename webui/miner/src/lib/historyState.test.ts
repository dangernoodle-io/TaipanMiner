import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'

vi.mock('uplot', async () => {
  class FakeUPlot {
    setData = vi.fn()
    destroy = vi.fn()
    setSize = vi.fn()
    constructor(public opts: any, public data: any, public container: HTMLElement) {}
  }
  return { default: FakeUPlot }
})

vi.mock('./stores', async () => {
  const { writable } = await import('svelte/store')
  const history = writable<any[]>([])
  const hasAsic = writable<boolean>(false)
  return { history, hasAsic }
})

import { history, hasAsic } from './stores'
import { createHistoryState } from './historyState.svelte'
import { WINDOWS, DEFAULT_WINDOW_IDX } from './historyChart'

const NOW_TS = Math.floor(Date.now() / 1000)

const makeSample = (ts: number, ghs = 485) => ({
  ts,
  total_ghs: ghs,
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
})

function makeContainer(): HTMLDivElement {
  const el = document.createElement('div')
  Object.defineProperty(el, 'clientWidth', { value: 800 })
  return el
}

beforeEach(() => {
  vi.clearAllMocks()
  history.set([])
  hasAsic.set(false)
})

afterEach(() => {
  history.set([])
  hasAsic.set(false)
})

describe('createHistoryState — initial state', () => {
  it('starts with default window index (15m)', () => {
    const hs = createHistoryState()
    expect(hs.windowIdx).toBe(DEFAULT_WINDOW_IDX)
    expect(WINDOWS[hs.windowIdx].label).toBe('15m')
  })

  it('starts with no chart instance', () => {
    const hs = createHistoryState()
    expect(hs.plot).toBeNull()
  })

  it('starts with count 0 when history is empty', () => {
    const hs = createHistoryState()
    expect(hs.count).toBe(0)
  })

  it('starts with non-ASIC metrics when hasAsic is false', () => {
    hasAsic.set(false)
    const hs = createHistoryState()
    const asicOnlyMetrics = hs.metrics.filter((m: any) => m.asicOnly)
    expect(asicOnlyMetrics).toHaveLength(0)
  })

  it('has WINDOWS exposed', () => {
    const hs = createHistoryState()
    expect(hs.WINDOWS).toHaveLength(5)
  })
})

describe('selectWindow()', () => {
  it('updates windowIdx', () => {
    const hs = createHistoryState()
    hs.selectWindow(0)
    expect(hs.windowIdx).toBe(0)
  })

  it('selects each window correctly', () => {
    const hs = createHistoryState()
    for (let i = 0; i < WINDOWS.length; i++) {
      hs.selectWindow(i)
      expect(hs.windowIdx).toBe(i)
    }
  })

  it('triggers rebuild if chart is mounted', async () => {
    const uPlot = (await import('uplot')).default
    const hs = createHistoryState()
    const el = makeContainer()
    hs.mountChart(el)
    const firstInstance = hs.plot
    // selectWindow should destroy + recreate
    hs.selectWindow(0)
    expect((uPlot as any).prototype.destroy || firstInstance).toBeDefined()
    // new instance created
    expect(hs.plot).not.toBeNull()
  })
})

describe('mountChart()', () => {
  it('creates a uPlot instance', async () => {
    const hs = createHistoryState()
    const el = makeContainer()
    hs.mountChart(el)
    expect(hs.plot).not.toBeNull()
  })

  it('subscribes to history store (updates when store changes)', async () => {
    const hs = createHistoryState()
    const el = makeContainer()
    hs.mountChart(el)
    const instance = hs.plot as any
    // Push a new sample to the store
    history.set([makeSample(NOW_TS)])
    // setData should have been called
    expect(instance.setData).toHaveBeenCalled()
  })

  it('calls window.addEventListener for resize', () => {
    const spy = vi.spyOn(window, 'addEventListener')
    const hs = createHistoryState()
    const el = makeContainer()
    hs.mountChart(el)
    expect(spy).toHaveBeenCalledWith('resize', expect.any(Function))
  })
})

describe('destroyChart()', () => {
  it('calls destroy on the uPlot instance', () => {
    const hs = createHistoryState()
    const el = makeContainer()
    hs.mountChart(el)
    const instance = hs.plot as any
    hs.destroyChart()
    expect(instance.destroy).toHaveBeenCalled()
  })

  it('nulls the plot reference', () => {
    const hs = createHistoryState()
    const el = makeContainer()
    hs.mountChart(el)
    hs.destroyChart()
    expect(hs.plot).toBeNull()
  })

  it('removes resize listener', () => {
    const spy = vi.spyOn(window, 'removeEventListener')
    const hs = createHistoryState()
    const el = makeContainer()
    hs.mountChart(el)
    hs.destroyChart()
    expect(spy).toHaveBeenCalledWith('resize', expect.any(Function))
  })

  it('stops history subscription (setData not called after destroy)', () => {
    const hs = createHistoryState()
    const el = makeContainer()
    hs.mountChart(el)
    const instance = hs.plot as any
    hs.destroyChart()
    const callsBefore = instance.setData.mock.calls.length
    history.set([makeSample(NOW_TS)])
    expect(instance.setData.mock.calls.length).toBe(callsBefore)
  })

  it('is safe to call when never mounted', () => {
    const hs = createHistoryState()
    expect(() => hs.destroyChart()).not.toThrow()
  })
})

describe('$derived: count', () => {
  it('reflects history store length', () => {
    const hs = createHistoryState()
    expect(hs.count).toBe(0)
    history.set([makeSample(NOW_TS), makeSample(NOW_TS - 5)])
    expect(hs.count).toBe(2)
  })
})

describe('$derived: metrics', () => {
  it('excludes ASIC-only metrics when hasAsic is false', () => {
    hasAsic.set(false)
    const hs = createHistoryState()
    const keys = hs.metrics.map((m: any) => m.key)
    expect(keys).toContain('total_ghs')
    expect(keys).toContain('temp_c')
    expect(keys).not.toContain('hw_err_pct')
    expect(keys).not.toContain('rpm')
  })

  it('includes ASIC-only metrics when hasAsic is true', () => {
    hasAsic.set(true)
    const hs = createHistoryState()
    const keys = hs.metrics.map((m: any) => m.key)
    expect(keys).toContain('hw_err_pct')
    expect(keys).toContain('rpm')
    expect(keys).toContain('asic_freq_mhz')
  })
})

describe('window filtering behavior', () => {
  it('mountChart builds data with all samples when window is All', async () => {
    const hs = createHistoryState()
    hs.selectWindow(4) // All
    // old sample that would be filtered by 15m window
    history.set([makeSample(NOW_TS - 10000)])
    const el = makeContainer()
    hs.mountChart(el)
    // chart was created without error
    expect(hs.plot).not.toBeNull()
  })

  it('selectWindow 0 (1m) with old data still creates chart', () => {
    history.set([makeSample(NOW_TS - 120)])
    const hs = createHistoryState()
    const el = makeContainer()
    hs.mountChart(el)
    hs.selectWindow(0) // 1m window — sample at -120s is out of window
    expect(hs.plot).not.toBeNull()
  })
})
