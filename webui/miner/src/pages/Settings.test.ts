import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render } from '@testing-library/svelte'
import { stats, info, fan, hasAsic } from '../lib/stores'

vi.mock('../lib/api', () => ({
  fetchStats: vi.fn(),
  fetchInfo: vi.fn(),
  fetchPower: vi.fn(),
  fetchFan: vi.fn(),
  fetchSettings: vi.fn().mockResolvedValue({ hostname: 'taipan', display_en: true, ota_skip_check: false }),
  fetchPool: vi.fn(),
  fetchHealth: vi.fn(),
  ping: vi.fn(),
  patchSettings: vi.fn()
}))

import Settings from './Settings.svelte'

const baseStats = {
  session_shares: 10, session_rejected: 1, lifetime_shares: 1000, last_share_ago_s: 30,
  best_diff: 500000, uptime_s: 3600, temp_c: 40, hashrate: 485e9, hashrate_avg: 480e9,
  hashrate_1m: null, hashrate_10m: null, hashrate_1h: null, shares: null, asic_hashrate: null,
  asic_hashrate_avg: null, asic_shares: null, asic_temp_c: 72, asic_freq_configured_mhz: 400,
  asic_freq_effective_mhz: 395, asic_small_cores: 256, asic_count: 2, expected_ghs: 485,
  asic_total_ghs: 485.5, asic_hw_error_pct: 0.01, asic_total_ghs_1m: 484, asic_total_ghs_10m: 486,
  asic_total_ghs_1h: 483, asic_hw_error_pct_1m: 0.01, asic_hw_error_pct_10m: 0.01,
  asic_hw_error_pct_1h: 0.02,
  hw_error_pct_1m: null, hw_error_pct_10m: null, hw_error_pct_1h: null,
  pool_effective_hashrate: null, rejected: null
}

const baseInfo = {
  board: 'bitaxe-601', project_name: 'TaipanMiner', version: 'v1.0.0', idf_version: '5.5.3',
  build_date: '2024-01-15', build_time: '14:30:00', chip_model: 'esp32-s3', cores: 2,
  mac: '00:11:22:33:44:55', ssid: 'TestNetwork', flash_size: 16777216, app_size: 1048576,
  total_heap: 262144, free_heap: 131072, reset_reason: 'Unknown', wdt_resets: 0,
  boot_time: 1705333200, worker_name: 'testworker', hostname: 'taipan.local', validated: true
}

describe('Settings', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    stats.set(null)
    info.set(null)
    fan.set(null)
    hasAsic.set(false)
  })

  it('renders without crashing', () => {
    const result = render(Settings)
    expect(result.component).toBeDefined()
  })

  it('renders with info', () => {
    info.set(baseInfo as any)
    const result = render(Settings)
    expect(result.component).toBeDefined()
  })

  it('renders without hasAsic', () => {
    stats.set(baseStats as any)
    hasAsic.set(false)
    const result = render(Settings)
    expect(result.component).toBeDefined()
  })

  it('renders with hasAsic', () => {
    stats.set({ ...baseStats, asic_count: 2 } as any)
    hasAsic.set(true)
    const result = render(Settings)
    expect(result.component).toBeDefined()
  })

  it('renders with multiple chips', () => {
    stats.set({ ...baseStats, asic_count: 3 } as any)
    hasAsic.set(true)
    const result = render(Settings)
    expect(result.component).toBeDefined()
  })

  it('renders with null chip count', () => {
    stats.set({ ...baseStats, asic_count: null } as any)
    hasAsic.set(true)
    const result = render(Settings)
    expect(result.component).toBeDefined()
  })

  it('renders with custom frequency', () => {
    stats.set({ ...baseStats, asic_freq_configured_mhz: 550 } as any)
    hasAsic.set(true)
    const result = render(Settings)
    expect(result.component).toBeDefined()
  })

  it('renders with null frequency', () => {
    stats.set({ ...baseStats, asic_freq_configured_mhz: null } as any)
    hasAsic.set(true)
    const result = render(Settings)
    expect(result.component).toBeDefined()
  })

  it('renders with device name', () => {
    info.set({ ...baseInfo, hostname: 'custom-miner.local' } as any)
    const result = render(Settings)
    expect(result.component).toBeDefined()
  })

  it('renders with board type', () => {
    info.set({ ...baseInfo, board: 'bitaxe-403' } as any)
    const result = render(Settings)
    expect(result.component).toBeDefined()
  })

  it('renders with worker name', () => {
    info.set({ ...baseInfo, worker_name: 'my-worker' } as any)
    const result = render(Settings)
    expect(result.component).toBeDefined()
  })

  it('renders with version', () => {
    info.set({ ...baseInfo, version: 'v2.5.1' } as any)
    const result = render(Settings)
    expect(result.component).toBeDefined()
  })

  it('renders with fan data', () => {
    fan.set({
      autofan: false, die_target_c: 65, vr_target_c: 80, min_pct: 35, manual_pct: 80,
      duty_pct: 80, rpm: 3200, pid_input_src: 'die', pid_input_c: null, die_ema_c: null,
      vr_ema_c: null
    })
    hasAsic.set(true)
    const result = render(Settings)
    expect(result.component).toBeDefined()
  })
})
