import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import { pool } from '../lib/stores'

// Prevent store from importing api/fetch
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

import PoolStrip from './PoolStrip.svelte'

describe('PoolStrip', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    pool.set(null)
  })

  it('shows dash when pool is null', () => {
    render(PoolStrip)
    // Host and diff show — when null
    const dashes = screen.getAllByText('—')
    expect(dashes.length).toBeGreaterThanOrEqual(1)
  })

  it('shows pool host and port when pool is set', () => {
    pool.set({
      host: 'pool.example.com',
      port: 3333,
      worker: 'miner-1',
      connected: true,
      active_pool_idx: 0,
      current_difficulty: 0,
      extranonce_subscribe_status: 'ok',
      configured: null,
      notify: null
    } as any)
    render(PoolStrip)
    expect(screen.getByText('pool.example.com:3333')).toBeInTheDocument()
  })

  it('shows worker name', () => {
    pool.set({
      host: 'pool.example.com',
      port: 3333,
      worker: 'my-worker',
      connected: true,
      active_pool_idx: 0,
      current_difficulty: 1024,
      extranonce_subscribe_status: 'ok',
      configured: null,
      notify: null
    } as any)
    render(PoolStrip)
    expect(screen.getByText('my-worker')).toBeInTheDocument()
  })

  it('shows difficulty', () => {
    pool.set({
      host: 'pool.example.com',
      port: 3333,
      worker: 'w',
      connected: true,
      active_pool_idx: 0,
      current_difficulty: 65536,
      extranonce_subscribe_status: 'ok',
      configured: null,
      notify: null
    } as any)
    render(PoolStrip)
    // fmtPoolDiff(65536) = "65536"
    expect(screen.getByText('65536')).toBeInTheDocument()
  })
})
