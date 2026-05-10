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
})
