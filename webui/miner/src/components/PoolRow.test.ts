import { describe, it, expect, vi } from 'vitest'
import { render, screen, fireEvent } from '@testing-library/svelte'
import PoolRow from './PoolRow.svelte'
import type { Pool } from '../lib/api'

const basePool: Pool = {
  host: 'pool.example.com',
  port: 3333,
  worker: 'miner-1',
  connected: true,
  active_pool_idx: 0,
  current_difficulty: 65536,
  extranonce_subscribe_status: 'ok',
  configured: {
    primary: {
      host: 'pool.example.com',
      port: 3333,
      wallet: 'bc1qtest0abc',
      worker: 'miner-1',
      extranonce_subscribe: true,
      decode_coinbase: true
    },
    fallback: null
  },
  notify: null
} as unknown as Pool

describe('PoolRow', () => {
  it('shows "not configured" when no cfg', () => {
    render(PoolRow, {
      props: { idx: 1, displayPool: { ...basePool, configured: { primary: basePool.configured!.primary, fallback: null } } }
    })
    expect(screen.getByText(/not configured/)).toBeInTheDocument()
  })

  it('shows kind label Primary', () => {
    render(PoolRow, { props: { idx: 0, displayPool: basePool } })
    expect(screen.getByText('Primary')).toBeInTheDocument()
  })

  it('shows kind label Fallback for idx=1', () => {
    const pool: Pool = {
      ...basePool,
      active_pool_idx: 1,
      connected: false,
      configured: {
        primary: basePool.configured!.primary,
        fallback: {
          host: 'backup.example.com',
          port: 4444,
          wallet: 'bc1qfallback',
          worker: 'worker2',
          extranonce_subscribe: false,
          decode_coinbase: false
        }
      }
    } as unknown as Pool
    render(PoolRow, { props: { idx: 1, displayPool: pool } })
    expect(screen.getByText('Fallback')).toBeInTheDocument()
  })

  it('shows host in configured row', () => {
    render(PoolRow, { props: { idx: 0, displayPool: basePool } })
    expect(screen.getByText('pool.example.com')).toBeInTheDocument()
  })

  it('shows Edit button', () => {
    render(PoolRow, { props: { idx: 0, displayPool: basePool } })
    expect(screen.getByRole('button', { name: 'Edit' })).toBeInTheDocument()
  })

  it('shows ACTIVE pill when pool is active and connected', () => {
    render(PoolRow, { props: { idx: 0, displayPool: basePool } })
    // Two ACTIVE elements: status-pill in caption row + decode status
    const activeEls = screen.getAllByText('ACTIVE')
    expect(activeEls.length).toBeGreaterThanOrEqual(1)
  })

  it('shows Configure button when unconfigured (idx=0 no cfg)', () => {
    const unconfigured: Pool = {
      ...basePool,
      configured: { primary: null, fallback: null }
    } as unknown as Pool
    render(PoolRow, { props: { idx: 0, displayPool: unconfigured } })
    expect(screen.getByRole('button', { name: 'Configure' })).toBeInTheDocument()
  })

  it('Edit button is rendered and clickable', async () => {
    render(PoolRow, { props: { idx: 0, displayPool: basePool } })
    const btn = screen.getByRole('button', { name: 'Edit' })
    // Just verify it exists and isn't disabled
    expect(btn).not.toBeDisabled()
  })
})
