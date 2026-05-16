import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render } from '@testing-library/svelte'
import { stats, info } from '../lib/stores'

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

import LiveTitle from './LiveTitle.svelte'

describe('LiveTitle', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    stats.set(null)
    info.set(null)
    document.title = 'TaipanMiner'
  })

  it('renders without crashing', () => {
    const { container } = render(LiveTitle)
    expect(container).toBeTruthy()
  })

  it('updates document.title when stats is set', async () => {
    render(LiveTitle)
    stats.set({
      asic_total_ghs: 485.5,
      asic_temp_c: 72,
      session_shares: 10,
      session_rejected: 0,
      lifetime: { shares: 100, best_diff: 500 },
      last_share_ago_s: 30,
      best_diff: 1000,
      uptime_s: 3600,
      temp_c: 40,
      hashrate: 0,
      hashrate_avg: 0,
      shares: null,
      asic_hashrate: null,
      asic_hashrate_avg: null,
      asic_shares: null,
      asic_freq_configured_mhz: null,
      asic_freq_effective_mhz: null,
      asic_small_cores: null,
      asic_count: null,
      expected_ghs: null,
      asic_hw_error_pct: null,
      asic_total_ghs_1m: null,
      asic_total_ghs_10m: null,
      asic_total_ghs_1h: null,
      asic_hw_error_pct_1m: null,
      asic_hw_error_pct_10m: null,
      asic_hw_error_pct_1h: null,
      pool_effective_hashrate: null,
      rejected: null
    } as any)
    // Allow reactive update
    await new Promise(r => setTimeout(r, 0))
    expect(document.title).toContain('TaipanMiner')
    expect(document.title).toContain('485.5 GH/s')
  })
})
