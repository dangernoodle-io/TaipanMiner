import { describe, it, expect } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import PoolStatsCard from './PoolStatsCard.svelte'
import type { PoolStat } from '../lib/api'

const basestat: PoolStat = {
  host: 'pool.example.com',
  port: 3333,
  shares: 142,
  hashes: 1500000000000,
  best_diff: 1234.5678,
  blocks_found: 0,
  last_seen_s: 45,
}

describe('PoolStatsCard', () => {
  it('renders host:port as title', () => {
    render(PoolStatsCard, {
      props: { stat: basestat, active: false },
    })
    expect(screen.getByText('pool.example.com:3333')).toBeInTheDocument()
  })

  it('shows blocks_found prominently when > 0', () => {
    const stat = { ...basestat, blocks_found: 3 }
    render(PoolStatsCard, {
      props: { stat, active: false },
    })
    expect(screen.getByText('3 blocks')).toBeInTheDocument()
  })

  it('does not show blocks badge when blocks_found is 0', () => {
    render(PoolStatsCard, {
      props: { stat: basestat, active: false },
    })
    expect(screen.queryByText(/blocks?/)).toBeNull()
  })

  it('applies active class when active is true', () => {
    const { container } = render(PoolStatsCard, {
      props: { stat: basestat, active: true },
    })
    expect(container.querySelector('.pool-stats-card')).toHaveClass('active')
  })

  it('does not apply active class when active is false', () => {
    const { container } = render(PoolStatsCard, {
      props: { stat: basestat, active: false },
    })
    expect(container.querySelector('.pool-stats-card')).not.toHaveClass('active')
  })

  it('renders all stat fields', () => {
    render(PoolStatsCard, {
      props: { stat: basestat, active: false },
    })
    expect(screen.getByText(/shares/i)).toBeInTheDocument()
    expect(screen.getByText(/hashes/i)).toBeInTheDocument()
    expect(screen.getByText(/best diff/i)).toBeInTheDocument()
    expect(screen.getByText(/last seen/i)).toBeInTheDocument()
  })

  it('formats hashes with appropriate SI suffix', () => {
    render(PoolStatsCard, {
      props: { stat: { ...basestat, hashes: 500000000000 }, active: false },
    })
    const text = screen.getByText(/hashes/).parentElement?.textContent ?? ''
    expect(text).toContain('GH')
  })

  it('uses singular "block" when blocks_found is 1', () => {
    const stat = { ...basestat, blocks_found: 1 }
    render(PoolStatsCard, {
      props: { stat, active: false },
    })
    expect(screen.getByText('1 block')).toBeInTheDocument()
  })

  it('omits last seen field when last_seen_s is 0', () => {
    render(PoolStatsCard, {
      props: { stat: { ...basestat, last_seen_s: 0 }, active: false },
    })
    expect(screen.queryByText(/last seen/i)).toBeNull()
  })
})
