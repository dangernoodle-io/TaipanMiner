import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render } from '@testing-library/svelte'
import { stats, info, pool } from '../lib/stores'

vi.mock('../lib/api', () => ({
  fetchStats: vi.fn(),
  fetchInfo: vi.fn(),
  fetchPower: vi.fn(),
  fetchFan: vi.fn(),
  fetchSettings: vi.fn(),
  fetchPool: vi.fn(),
  fetchHealth: vi.fn(),
  ping: vi.fn(),
  putPool: vi.fn(),
  switchPool: vi.fn(),
  deletePoolSlot: vi.fn()
}))

import Pool from './Pool.svelte'

const basePool = {
  host: 'pool.example.com',
  port: 3333,
  worker: 'worker1',
  wallet: 'wallet1',
  connected: true,
  session_start_ago_s: 10,
  current_difficulty: 1024,
  pool_effective_hashrate: 1e12,
  pool_effective_hashrate_1m: 1e12,
  pool_effective_hashrate_10m: 1e12,
  pool_effective_hashrate_1h: 1e12,
  latency_ms: 50,
  extranonce1: 'abc123',
  extranonce2_size: 4,
  version_mask: '00000000',
  notify: null,
  active_pool_idx: 0 as const,
  extranonce_subscribe_status: 'active' as const,
  configured: {
    primary: {
      host: 'pool.example.com',
      port: 3333,
      worker: 'worker1',
      wallet: 'wallet1',
      extranonce_subscribe: true,
      decode_coinbase: true
    },
    fallback: null
  }
}

describe('Pool', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    stats.set(null)
    info.set(null)
    pool.set(null)
  })

  it('renders without crashing when null', () => {
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with pool data', () => {
    pool.set(basePool as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders when connected', () => {
    pool.set({ ...basePool, connected: true } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders when disconnected', () => {
    pool.set({ ...basePool, connected: false } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with difficulty', () => {
    pool.set({ ...basePool, current_difficulty: 2048 } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with hashrate', () => {
    pool.set({ ...basePool, pool_effective_hashrate: 2e12 } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with latency', () => {
    pool.set({ ...basePool, latency_ms: 75 } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with extranonce1', () => {
    pool.set({ ...basePool, extranonce1: 'deadbeef' } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with session time', () => {
    pool.set({ ...basePool, session_start_ago_s: 300 } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with notify', () => {
    pool.set({
      ...basePool,
      notify: {
        job_id: 'job1',
        prev_hash: '0000',
        coinb1: 'c1',
        coinb2: 'c2',
        merkle_branches: [],
        version: '20000000',
        nbits: '1701453b',
        ntime: '67ac6400',
        clean_jobs: false
      }
    } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with fallback pool', () => {
    pool.set({
      ...basePool,
      configured: {
        primary: basePool.configured.primary,
        fallback: {
          host: 'fallback.example.com',
          port: 3334,
          worker: 'worker2',
          wallet: 'wallet2',
          extranonce_subscribe: false,
          decode_coinbase: false
        }
      }
    } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with null active pool', () => {
    pool.set({ ...basePool, active_pool_idx: null } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with pending subscribe', () => {
    pool.set({ ...basePool, extranonce_subscribe_status: 'pending' } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with rejected subscribe', () => {
    pool.set({ ...basePool, extranonce_subscribe_status: 'rejected' } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with off subscribe', () => {
    pool.set({ ...basePool, extranonce_subscribe_status: 'off' } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with rolling rates', () => {
    pool.set({
      ...basePool,
      pool_effective_hashrate_1m: 0.9e12,
      pool_effective_hashrate_10m: 1.0e12,
      pool_effective_hashrate_1h: 1.05e12
    } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with null hashrate', () => {
    pool.set({ ...basePool, pool_effective_hashrate: null } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with null latency', () => {
    pool.set({ ...basePool, latency_ms: null } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with null session time', () => {
    pool.set({ ...basePool, session_start_ago_s: null } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with null extranonce1', () => {
    pool.set({ ...basePool, extranonce1: null } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with high difficulty', () => {
    pool.set({ ...basePool, current_difficulty: 1000000 } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with zero difficulty', () => {
    pool.set({ ...basePool, current_difficulty: 0 } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with empty extranonce1', () => {
    pool.set({ ...basePool, extranonce1: '' } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders notify with short coinb1', () => {
    pool.set({
      ...basePool,
      notify: {
        job_id: 'job1',
        prev_hash: '0000',
        coinb1: 'abc',
        coinb2: 'def',
        merkle_branches: [],
        version: '20000000',
        nbits: '1701453b',
        ntime: '67ac6400',
        clean_jobs: true
      }
    } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders notify with multiple merkle branches', () => {
    pool.set({
      ...basePool,
      notify: {
        job_id: 'job1',
        prev_hash: '0000',
        coinb1: 'c1',
        coinb2: 'c2',
        merkle_branches: ['abc', 'def', 'ghi'],
        version: '20000000',
        nbits: '1701453b',
        ntime: '67ac6400',
        clean_jobs: false
      }
    } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with both primary and fallback configured', () => {
    pool.set({
      ...basePool,
      configured: {
        primary: {
          host: 'primary.pool.com',
          port: 3333,
          worker: 'primary_worker',
          wallet: 'primary_wallet',
          extranonce_subscribe: true,
          decode_coinbase: true
        },
        fallback: {
          host: 'fallback.pool.com',
          port: 3334,
          worker: 'fallback_worker',
          wallet: 'fallback_wallet',
          extranonce_subscribe: false,
          decode_coinbase: false
        }
      }
    } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with only fallback configured', () => {
    pool.set({
      ...basePool,
      configured: {
        primary: null,
        fallback: {
          host: 'fallback.pool.com',
          port: 3334,
          worker: 'fallback_worker',
          wallet: 'fallback_wallet',
          extranonce_subscribe: false,
          decode_coinbase: true
        }
      }
    } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with no configured pools', () => {
    pool.set({
      ...basePool,
      configured: { primary: null, fallback: null }
    } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with active primary pool', () => {
    pool.set({ ...basePool, active_pool_idx: 0 } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with active fallback pool', () => {
    pool.set({ ...basePool, active_pool_idx: 1 } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders subscribe pending state', () => {
    pool.set({ ...basePool, extranonce_subscribe_status: 'pending' } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders subscribe rejected state', () => {
    pool.set({ ...basePool, extranonce_subscribe_status: 'rejected' } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders subscribe off state', () => {
    pool.set({ ...basePool, extranonce_subscribe_status: 'off' } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with only 1m rolling rate', () => {
    pool.set({
      ...basePool,
      pool_effective_hashrate_1m: 0.95e12,
      pool_effective_hashrate_10m: null,
      pool_effective_hashrate_1h: null
    } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with only 10m rolling rate', () => {
    pool.set({
      ...basePool,
      pool_effective_hashrate_1m: null,
      pool_effective_hashrate_10m: 0.98e12,
      pool_effective_hashrate_1h: null
    } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with only 1h rolling rate', () => {
    pool.set({
      ...basePool,
      pool_effective_hashrate_1m: null,
      pool_effective_hashrate_10m: null,
      pool_effective_hashrate_1h: 1.02e12
    } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with all null rolling rates', () => {
    pool.set({
      ...basePool,
      pool_effective_hashrate_1m: null,
      pool_effective_hashrate_10m: null,
      pool_effective_hashrate_1h: null
    } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with very large latency', () => {
    pool.set({ ...basePool, latency_ms: 5000 } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with zero latency', () => {
    pool.set({ ...basePool, latency_ms: 0 } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with very long session', () => {
    pool.set({ ...basePool, session_start_ago_s: 86400 } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with fresh session', () => {
    pool.set({ ...basePool, session_start_ago_s: 1 } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with no notify', () => {
    pool.set({ ...basePool, notify: null } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with empty notify branches', () => {
    pool.set({
      ...basePool,
      notify: {
        job_id: '',
        prev_hash: '',
        coinb1: '',
        coinb2: '',
        merkle_branches: [],
        version: '',
        nbits: '',
        ntime: '',
        clean_jobs: false
      }
    } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with notify clean_jobs true', () => {
    pool.set({
      ...basePool,
      notify: {
        job_id: 'j1',
        prev_hash: 'ph',
        coinb1: 'c1',
        coinb2: 'c2',
        merkle_branches: [],
        version: 'v',
        nbits: 'b',
        ntime: 't',
        clean_jobs: true
      }
    } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with disconnected when no session', () => {
    pool.set({ ...basePool, connected: false, session_start_ago_s: null } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with null extranonce2_size', () => {
    pool.set({ ...basePool, extranonce2_size: null } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with null version_mask', () => {
    pool.set({ ...basePool, version_mask: null } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with very large extranonce2_size', () => {
    pool.set({ ...basePool, extranonce2_size: 255 } as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })

  it('renders with stats and pool together', () => {
    stats.set({
      session_shares: 100, session_rejected: 5, lifetime: { shares: 1000, best_diff: 500 },
      last_share_ago_s: 10, best_diff: 1000, uptime_s: 36000,
      temp_c: 60, hashrate: 1e9, hashrate_avg: 1.1e9, shares: 100,
      asic_hashrate: null, asic_hashrate_avg: null, asic_shares: null,
      asic_temp_c: null, asic_freq_configured_mhz: null, asic_freq_effective_mhz: null,
      asic_small_cores: null, asic_count: null, expected_ghs: null,
      asic_total_ghs: null, asic_hw_error_pct: null, asic_total_ghs_1m: null,
      asic_total_ghs_10m: null, asic_total_ghs_1h: null, asic_hw_error_pct_1m: null,
      asic_hw_error_pct_10m: null, asic_hw_error_pct_1h: null, hashrate_1m: null,
      hashrate_10m: null, hashrate_1h: null, hw_error_pct_1m: null,
      hw_error_pct_10m: null, hw_error_pct_1h: null, pool_effective_hashrate: 1e12
    } as any)
    pool.set(basePool as any)
    const result = render(Pool)
    expect(result.component).toBeDefined()
  })
})
