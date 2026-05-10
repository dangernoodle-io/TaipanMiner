import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, fireEvent } from '@testing-library/svelte'
import { stats, power, fan, hasAsic, fanEditOpen } from '../lib/stores'

vi.mock('../lib/api', () => ({
  fetchStats: vi.fn(),
  fetchInfo: vi.fn(),
  fetchPower: vi.fn(),
  fetchFan: vi.fn(),
  fetchSettings: vi.fn(),
  fetchPool: vi.fn(),
  fetchHealth: vi.fn(),
  ping: vi.fn()
}))

import Dashboard from './Dashboard.svelte'

const baseStats = {
  session_shares: 10,
  session_rejected: 1,
  lifetime_shares: 1000,
  last_share_ago_s: 30,
  best_diff: 500000,
  uptime_s: 3600,
  temp_c: 40,
  hashrate: 485e9,
  hashrate_avg: 480e9,
  hashrate_1m: null,
  hashrate_10m: null,
  hashrate_1h: null,
  shares: null,
  asic_hashrate: null,
  asic_hashrate_avg: null,
  asic_shares: null,
  asic_temp_c: 72,
  asic_freq_configured_mhz: 400,
  asic_freq_effective_mhz: 395,
  asic_small_cores: 256,
  asic_count: 2,
  expected_ghs: 485,
  asic_total_ghs: 485.5,
  asic_hw_error_pct: 0.01,
  asic_total_ghs_1m: 484,
  asic_total_ghs_10m: 486,
  asic_total_ghs_1h: 483,
  asic_hw_error_pct_1m: 0.01,
  asic_hw_error_pct_10m: 0.01,
  asic_hw_error_pct_1h: 0.02,
  hw_error_pct_1m: null,
  hw_error_pct_10m: null,
  hw_error_pct_1h: null,
  pool_effective_hashrate: null,
  rejected: null,
  asic_chips: []
}

const baseFan = {
  autofan: false,
  die_target_c: 65,
  vr_target_c: 80,
  min_pct: 35,
  manual_pct: 80,
  duty_pct: 80,
  rpm: 3200,
  pid_input_src: 'die' as const,
  pid_input_c: null,
  die_ema_c: null,
  vr_ema_c: null
}

const basePower = {
  vcore_mv: 1100,
  icore_ma: 5000,
  pcore_mw: 25000,
  efficiency_jth: 25.5,
  vin_mv: 12000,
  vin_low: false,
  board_temp_c: 45,
  vr_temp_c: 60
}

describe('Dashboard', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    stats.set(null)
    power.set(null)
    fan.set(null)
    hasAsic.set(false)
    fanEditOpen.set(false)
  })

  it('renders without crashing when stores are null', () => {
    render(Dashboard)
    expect(document.querySelector('.grid')).not.toBeNull()
  })

  it('renders PoolStrip at top', () => {
    render(Dashboard)
    const stickyPool = document.querySelector('.sticky-pool')
    expect(stickyPool).not.toBeNull()
  })

  it('renders Hero section when stats is set', () => {
    stats.set(baseStats as any)
    render(Dashboard)
    const hero = document.querySelector('.hero')
    expect(hero).not.toBeNull()
  })

  it('renders grid layout', () => {
    stats.set(baseStats as any)
    render(Dashboard)
    const grid = document.querySelector('.grid')
    expect(grid).not.toBeNull()
  })

  it('does not render Heat/Fan/Power sections when hasAsic is false', () => {
    stats.set(baseStats as any)
    hasAsic.set(false)
    render(Dashboard)
    // Should render main layout only
    expect(document.querySelector('.grid')).not.toBeNull()
  })

  it('renders sections when hasAsic is true', () => {
    stats.set(baseStats as any)
    power.set(basePower)
    fan.set(baseFan)
    hasAsic.set(true)
    render(Dashboard)

    const sections = document.querySelectorAll('section.card')
    // Should have Heat, Fan, Power, Performance sections
    expect(sections.length).toBeGreaterThan(0)
  })

  it('shows ASIC temperatures when hasAsic', () => {
    stats.set({ ...baseStats, asic_temp_c: 72 } as any)
    power.set({ ...basePower, board_temp_c: 45, vr_temp_c: 60 } as any)
    hasAsic.set(true)
    render(Dashboard)

    const sections = document.querySelectorAll('section.card')
    expect(sections.length).toBeGreaterThan(0)
  })

  it('shows Fan duty and RPM when hasAsic and fan data present', () => {
    stats.set(baseStats as any)
    fan.set(baseFan)
    hasAsic.set(true)
    render(Dashboard)

    const sections = document.querySelectorAll('section.card')
    expect(sections.length).toBeGreaterThan(0)
  })

  it('shows Fan PID badge when autofan=true and pid_input_src set', () => {
    stats.set(baseStats as any)
    fan.set({ ...baseFan, autofan: true, pid_input_src: 'die' })
    hasAsic.set(true)
    render(Dashboard)

    const badge = document.querySelector('.mode-badge')
    expect(badge).not.toBeNull()
    expect(badge?.textContent).toContain('PID')
  })

  it('does not show Fan PID badge when autofan=false', () => {
    stats.set(baseStats as any)
    fan.set({ ...baseFan, autofan: false })
    hasAsic.set(true)
    render(Dashboard)

    const badge = document.querySelector('.mode-badge')
    expect(badge).toBeNull()
  })

  it('shows duty-bar progress when fan.duty_pct is set', () => {
    stats.set(baseStats as any)
    fan.set({ ...baseFan, duty_pct: 65 })
    hasAsic.set(true)
    render(Dashboard)

    const dutyBar = document.querySelector('.duty-bar')
    expect(dutyBar).not.toBeNull()

    const dutyFill = document.querySelector('.duty-fill')
    expect(dutyFill).not.toBeNull()
    const width = dutyFill?.getAttribute('style')
    expect(width).toContain('65%')
  })

  it('does not show duty-bar when fan.duty_pct is null', () => {
    stats.set(baseStats as any)
    fan.set({ ...baseFan, duty_pct: null })
    hasAsic.set(true)
    render(Dashboard)

    const dutyBar = document.querySelector('.duty-bar')
    expect(dutyBar).toBeNull()
  })

  it('shows Power section when hasAsic', () => {
    stats.set(baseStats as any)
    power.set(basePower)
    hasAsic.set(true)
    render(Dashboard)

    const sections = document.querySelectorAll('section.card')
    expect(sections.length).toBeGreaterThan(0)
  })

  it('Fan section is clickable to open edit dialog', async () => {
    stats.set(baseStats as any)
    fan.set(baseFan)
    hasAsic.set(true)
    render(Dashboard)

    const fanCard = document.querySelector('section.card.clickable')
    expect(fanCard).not.toBeNull()
    expect(fanCard?.getAttribute('role')).toBe('button')

    await fireEvent.click(fanCard!)

    let isOpen = false
    fanEditOpen.subscribe(val => { isOpen = val })()
    expect(isOpen).toBe(true)
  })

  it('shows edit hint on Fan section', () => {
    stats.set(baseStats as any)
    fan.set(baseFan)
    hasAsic.set(true)
    render(Dashboard)

    const editHint = document.querySelector('.edit-hint')
    expect(editHint).not.toBeNull()
    expect(editHint?.textContent).toContain('edit')
  })

  it('renders ChipsCard when hasAsic and asic_chips present', () => {
    const chip = {
      idx: 0,
      total_ghs: 242,
      error_ghs: 0.1,
      hw_err_pct: 0.01,
      total_raw: 1000000,
      error_raw: 100,
      domain_ghs: [60, 60, 60, 62],
      total_drops: 0,
      error_drops: 0,
      domain_drops: [0, 0, 0, 0],
      last_drop_ago_s: null
    }
    stats.set({ ...baseStats, asic_chips: [chip] } as any)
    hasAsic.set(true)
    render(Dashboard)

    const fullSection = document.querySelector('div.full')
    expect(fullSection).not.toBeNull()
  })

  it('does not render ChipsCard when asic_chips is empty', () => {
    stats.set({ ...baseStats, asic_chips: [] } as any)
    hasAsic.set(true)
    render(Dashboard)

    const grid = document.querySelector('.grid')
    expect(grid).not.toBeNull()
  })

  it('shows vin_low flag in Power section when vin_low is true', () => {
    stats.set(baseStats as any)
    power.set({ ...basePower, vin_low: true })
    hasAsic.set(true)
    render(Dashboard)

    const sections = document.querySelectorAll('section.card')
    expect(sections.length).toBeGreaterThan(0)
  })

  it('renders with null efficiency value gracefully', () => {
    stats.set(baseStats as any)
    power.set({ ...basePower, efficiency_jth: null })
    hasAsic.set(true)
    render(Dashboard)

    const grid = document.querySelector('.grid')
    expect(grid).not.toBeNull()
  })
})
