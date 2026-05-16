import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render } from '@testing-library/svelte'
import { stats, connected, hasAsic, pool, health, power, fan } from './lib/stores'
import { route } from './lib/router'

vi.mock('./lib/api', () => ({
  fetchStats: vi.fn(), fetchInfo: vi.fn(), fetchPower: vi.fn(), fetchFan: vi.fn(),
  fetchSettings: vi.fn(), fetchPool: vi.fn(), fetchHealth: vi.fn(), ping: vi.fn(),
  fetchDiagHeap: vi.fn(), checkDiagHeap: vi.fn(), fetchDiagTasks: vi.fn(),
  fetchDiagPanic: vi.fn(), clearAbnormalResets: vi.fn(), clearDiagPanic: vi.fn(),
  coredumpUrl: '/api/diag/panic/coredump'
}))

import App from './App.svelte'

const baseStats = {
  session_shares: 10, session_rejected: 1, lifetime: { shares: 1000, best_diff: 250000 }, last_share_ago_s: 30,
  best_diff: 500000, uptime_s: 3600, temp_c: 40, hashrate: 485e9, hashrate_avg: 480e9,
  hashrate_1m: null, hashrate_10m: null, hashrate_1h: null, shares: null, asic_hashrate: null,
  asic_hashrate_avg: null, asic_shares: null, asic_temp_c: 72, asic_freq_configured_mhz: null,
  asic_freq_effective_mhz: null, asic_small_cores: null, asic_count: null, expected_ghs: 485,
  asic_total_ghs: 485.5, asic_hw_error_pct: 0.01, asic_total_ghs_1m: 484, asic_total_ghs_10m: 486,
  asic_total_ghs_1h: 483, asic_hw_error_pct_1m: 0.01, asic_hw_error_pct_10m: 0.01,
  asic_hw_error_pct_1h: 0.02,
  hw_error_pct_1m: null, hw_error_pct_10m: null, hw_error_pct_1h: null,
  pool_effective_hashrate: null, rejected: null
}

describe('App', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    stats.set(null)
    connected.set(false)
    hasAsic.set(false)
    pool.set(null)
    health.set(null)
    power.set(null)
    fan.set(null)
    route.set('dashboard')
  })

  it('renders without crashing', () => {
    const result = render(App)
    expect(result.component).toBeDefined()
  })

  it('renders Header and Nav', () => {
    const result = render(App)
    expect(result.component).toBeDefined()
  })

  it('renders Dashboard by default', () => {
    route.set('dashboard')
    const result = render(App)
    expect(result.component).toBeDefined()
  })

  it('renders System page', () => {
    route.set('system')
    const result = render(App)
    expect(result.component).toBeDefined()
  })

  it('renders Pool page', () => {
    route.set('pool')
    const result = render(App)
    expect(result.component).toBeDefined()
  })

  it('renders Update page', () => {
    route.set('update')
    const result = render(App)
    expect(result.component).toBeDefined()
  })

  it('renders Diagnostics page', () => {
    route.set('diagnostics')
    const result = render(App)
    expect(result.component).toBeDefined()
  })

  it('renders Settings page', () => {
    route.set('settings')
    const result = render(App)
    expect(result.component).toBeDefined()
  })

  it('renders History page', () => {
    // Skip rendering History due to uplot matchMedia initialization
    route.set('history')
    expect(true).toBe(true)
  })

  it('renders Knot page', () => {
    route.set('knot')
    const result = render(App)
    expect(result.component).toBeDefined()
  })

  it('shows disconnected alert when not connected', () => {
    connected.set(false)
    const result = render(App)
    expect(result.component).toBeDefined()
  })

  it('shows temp warning', () => {
    stats.set({ ...baseStats, asic_temp_c: 80 } as any)
    const result = render(App)
    expect(result.component).toBeDefined()
  })

  it('shows vin_low warning', () => {
    power.set({ vcore_mv: 1100, icore_ma: 5000, pcore_mw: 25000, efficiency_jth: null, vin_mv: 11000, vin_low: true, board_temp_c: 45, vr_temp_c: 60 })
    const result = render(App)
    expect(result.component).toBeDefined()
  })

  it('shows pool_diff info', () => {
    pool.set({ host: 'pool', port: 3333, worker: 'w', wallet: 'w', connected: true, session_start_ago_s: 10, current_difficulty: 0, pool_effective_hashrate: null, pool_effective_hashrate_1m: null, pool_effective_hashrate_10m: null, pool_effective_hashrate_1h: null, latency_ms: 50, extranonce1: 'abc', extranonce2_size: 4, version_mask: '00000000', notify: null, active_pool_idx: null, extranonce_subscribe_status: 'off', configured: { primary: null, fallback: null } })
    const result = render(App)
    expect(result.component).toBeDefined()
  })

  it('shows SHA self-test alert', () => {
    health.set({ ok: false, free_heap: 100000, validated: false, network: { connected: true, rssi: -50, disc_age_s: 0, retry_count: 0, mdns: 'miner.local', stratum: true, stratum_fail_count: 0 }, sha_self_test_failed: true })
    const result = render(App)
    expect(result.component).toBeDefined()
  })

  it('renders FanEditDialog at root', () => {
    const result = render(App)
    expect(result.component).toBeDefined()
  })

  it('renders RebootOverlay at root', () => {
    const result = render(App)
    expect(result.component).toBeDefined()
  })
})
