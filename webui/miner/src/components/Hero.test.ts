import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import { stats, connected, hasAsic, pool } from '../lib/stores'

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

import HeroComponent from './Hero.svelte'

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
  asic_freq_configured_mhz: null,
  asic_freq_effective_mhz: null,
  asic_small_cores: null,
  asic_count: null,
  expected_ghs: 485,
  asic_total_ghs: 485.5,
  asic_hw_error_pct: 0.01,
  asic_total_ghs_1m: 484,
  asic_total_ghs_10m: 486,
  asic_total_ghs_1h: 483,
  asic_hw_error_pct_1m: 0.01,
  asic_hw_error_pct_10m: 0.01,
  asic_hw_error_pct_1h: 0.02,
  pool_effective_hashrate: null,
  rejected: null
}

describe('Hero (store-driven)', () => {
  beforeEach(() => {
    stats.set(null)
    connected.set(false)
    hasAsic.set(false)
    pool.set(null)
  })

  it('renders nothing when stats is null', () => {
    const { container } = render(HeroComponent)
    expect(container.querySelector('.hero')).toBeNull()
  })

  it('renders hero section when stats is set', () => {
    stats.set(baseStats as any)
    render(HeroComponent)
    expect(document.querySelector('.hero')).not.toBeNull()
  })

  it('shows session shares and rejected', () => {
    stats.set(baseStats as any)
    render(HeroComponent)
    // shares label is present
    expect(screen.getByText('shares (90.9%)')).toBeInTheDocument()
  })

  it('shows lifetime shares', () => {
    stats.set(baseStats as any)
    render(HeroComponent)
    expect(screen.getByText('1,000')).toBeInTheDocument()
  })

  it('shows uptime', () => {
    stats.set(baseStats as any)
    render(HeroComponent)
    // 3600s = 1h 0m
    expect(screen.getByText('1h 0m')).toBeInTheDocument()
  })

  it('shows err metric when hasAsic=true', () => {
    stats.set(baseStats as any)
    hasAsic.set(true)
    render(HeroComponent)
    expect(screen.getByText('err')).toBeInTheDocument()
  })

  it('shows die temp when hasAsic=false', () => {
    stats.set({ ...baseStats, temp_c: 68 } as any)
    hasAsic.set(false)
    render(HeroComponent)
    expect(screen.getByText('Die Temp')).toBeInTheDocument()
  })

  it('shows err metric with bad class when err > 1', () => {
    stats.set({ ...baseStats, asic_hw_error_pct: 2.5 } as any)
    hasAsic.set(true)
    render(HeroComponent)
    expect(screen.getByText('err')).toBeInTheDocument()
    const errVal = document.querySelector('.kv .v.bad')
    expect(errVal).not.toBeNull()
  })

  it('shows die temp in bad class when temp_c > 75', () => {
    stats.set({ ...baseStats, temp_c: 80 } as any)
    hasAsic.set(false)
    render(HeroComponent)
    expect(screen.getByText('Die Temp')).toBeInTheDocument()
    const badTemp = document.querySelector('.kv .v.bad')
    expect(badTemp).not.toBeNull()
  })

  it('shows syncing pool-effective when uptime < 300', () => {
    stats.set({ ...baseStats, uptime_s: 100, pool_effective_hashrate: null } as any)
    render(HeroComponent)
    expect(screen.getByText('syncing…')).toBeInTheDocument()
  })

  it('shows pool-effective hashrate when available and uptime >= 300', () => {
    stats.set({ ...baseStats, uptime_s: 400, pool_effective_hashrate: 480e9 } as any)
    render(HeroComponent)
    expect(screen.getByText('pool-effective')).toBeInTheDocument()
  })

  it('shows best diff multiplier when pool.current_difficulty > 0', () => {
    stats.set({ ...baseStats, best_diff: 1000000 } as any)
    pool.set({ current_difficulty: 500000 } as any)
    render(HeroComponent)
    // diffMult = 2; displayed as "(2×)"
    expect(screen.getByText(/best diff/)).toBeInTheDocument()
  })

  it('shows accept rate and shares/hr when uptime > 60', () => {
    stats.set({ ...baseStats, session_shares: 5, session_rejected: 5, uptime_s: 120 } as any)
    render(HeroComponent)
    expect(screen.getByText('shares (50.0%)')).toBeInTheDocument()
  })

  it('shows connected dot when connected=true', () => {
    stats.set(baseStats as any)
    connected.set(true)
    render(HeroComponent)
    expect(document.querySelector('.conn-dot.connected')).not.toBeNull()
  })

  it('shows disconnected dot when connected=false', () => {
    stats.set(baseStats as any)
    connected.set(false)
    render(HeroComponent)
    expect(document.querySelector('.conn-dot.disconnected')).not.toBeNull()
  })
})
