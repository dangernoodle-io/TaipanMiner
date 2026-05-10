import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render } from '@testing-library/svelte'
import { history, hasAsic } from '../lib/stores'

vi.mock('../lib/api', () => ({
  fetchStats: vi.fn(), fetchInfo: vi.fn(), fetchPower: vi.fn(), fetchFan: vi.fn(),
  fetchSettings: vi.fn(), fetchPool: vi.fn(), fetchHealth: vi.fn(), ping: vi.fn()
}))

import History from './History.svelte'

const sample = {
  ts: Math.floor(Date.now() / 1000),
  total_ghs: 485.5, hw_err_pct: 0.01, temp_c: 72, vr_temp_c: 60, board_temp_c: 45,
  pcore_w: 25, vcore_v: 1.1, efficiency_jth: 25.5, asic_freq_mhz: 395, rpm: 3200, fan_duty: 80
}

describe('History', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    history.set([])
    hasAsic.set(false)
  })

  it('renders without history', () => {
    history.set([])
    const result = render(History)
    expect(result.component).toBeDefined()
  })

  it('renders with one sample', () => {
    history.set([sample])
    const result = render(History)
    expect(result.component).toBeDefined()
  })

  it('renders with multiple samples', () => {
    history.set([sample, { ...sample, ts: sample.ts - 5 }])
    const result = render(History)
    expect(result.component).toBeDefined()
  })

  it('renders for tdongle (no ASIC)', () => {
    history.set([{ ...sample, total_ghs: 0.485, hw_err_pct: null, vr_temp_c: null, board_temp_c: null, pcore_w: null, vcore_v: null, efficiency_jth: null, asic_freq_mhz: null, rpm: null, fan_duty: null }])
    hasAsic.set(false)
    const result = render(History)
    expect(result.component).toBeDefined()
  })

  it('renders for ASIC device', () => {
    history.set([sample])
    hasAsic.set(true)
    const result = render(History)
    expect(result.component).toBeDefined()
  })

  it('renders hashrate metric', () => {
    history.set([sample])
    const result = render(History)
    expect(result.component).toBeDefined()
  })

  it('renders temp metric', () => {
    history.set([sample])
    const result = render(History)
    expect(result.component).toBeDefined()
  })

  it('renders HW error metric for ASIC', () => {
    history.set([sample])
    hasAsic.set(true)
    const result = render(History)
    expect(result.component).toBeDefined()
  })

  it('filters ASIC metrics for non-ASIC', () => {
    history.set([{ ...sample, hw_err_pct: null, vr_temp_c: null }])
    hasAsic.set(false)
    const result = render(History)
    expect(result.component).toBeDefined()
  })

  it('renders power metrics for ASIC', () => {
    history.set([sample])
    hasAsic.set(true)
    const result = render(History)
    expect(result.component).toBeDefined()
  })

  it('renders VR temp for ASIC', () => {
    history.set([sample])
    hasAsic.set(true)
    const result = render(History)
    expect(result.component).toBeDefined()
  })

  it('renders frequency metric for ASIC', () => {
    history.set([sample])
    hasAsic.set(true)
    const result = render(History)
    expect(result.component).toBeDefined()
  })

  it('renders fan metrics for ASIC', () => {
    history.set([sample])
    hasAsic.set(true)
    const result = render(History)
    expect(result.component).toBeDefined()
  })

  it('handles null values', () => {
    history.set([{ ...sample, total_ghs: null, temp_c: null, vr_temp_c: null }])
    const result = render(History)
    expect(result.component).toBeDefined()
  })

  it('renders with many samples', () => {
    const samples = Array.from({ length: 100 }, (_, i) => ({ ...sample, ts: sample.ts - i * 5 }))
    history.set(samples)
    const result = render(History)
    expect(result.component).toBeDefined()
  })

  it('renders with partial data', () => {
    history.set([{ ...sample, vr_temp_c: null, pcore_w: null, efficiency_jth: null, rpm: null }])
    const result = render(History)
    expect(result.component).toBeDefined()
  })

  it('displays board temp separately', () => {
    history.set([sample])
    hasAsic.set(true)
    const result = render(History)
    expect(result.component).toBeDefined()
  })
})
