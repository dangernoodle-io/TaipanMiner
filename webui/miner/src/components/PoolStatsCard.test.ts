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
  it('renders host:port heading when not active', () => {
    render(PoolStatsCard, { props: { stat: basestat, active: false } })
    expect(screen.getByText('pool.example.com:3333')).toBeInTheDocument()
  })

  it('omits host:port heading when active (parent shows it)', () => {
    render(PoolStatsCard, { props: { stat: basestat, active: true } })
    expect(screen.queryByText('pool.example.com:3333')).toBeNull()
  })

  it('renders shares / hashes / best diff / blocks fields', () => {
    render(PoolStatsCard, { props: { stat: basestat, active: false } })
    expect(screen.getByText(/^shares$/i)).toBeInTheDocument()
    expect(screen.getByText(/^hashes$/i)).toBeInTheDocument()
    expect(screen.getByText(/^best diff$/i)).toBeInTheDocument()
    expect(screen.getByText(/^blocks$/i)).toBeInTheDocument()
  })

  it('renders blocks count value', () => {
    const stat = { ...basestat, blocks_found: 3 }
    const { container } = render(PoolStatsCard, { props: { stat, active: false } })
    const blocksLabel = container.querySelector('.stat-item:nth-of-type(4) .stat-value')
    expect(blocksLabel?.textContent?.trim()).toBe('3')
  })

  it('renders 0 blocks without highlight when blocks_found is 0', () => {
    const { container } = render(PoolStatsCard, { props: { stat: basestat, active: false } })
    const blocksValue = container.querySelector('.stat-item:nth-of-type(4) .stat-value')
    expect(blocksValue?.textContent?.trim()).toBe('0')
    expect(blocksValue?.classList.contains('blocks-found')).toBe(false)
  })

  it('applies blocks-found highlight when blocks_found > 0', () => {
    const stat = { ...basestat, blocks_found: 1 }
    const { container } = render(PoolStatsCard, { props: { stat, active: false } })
    const blocksValue = container.querySelector('.stat-item:nth-of-type(4) .stat-value')
    expect(blocksValue?.classList.contains('blocks-found')).toBe(true)
  })

  it('formats hashes with appropriate SI suffix', () => {
    render(PoolStatsCard, {
      props: { stat: { ...basestat, hashes: 500000000000 }, active: false },
    })
    const text = screen.getByText(/^hashes$/i).parentElement?.textContent ?? ''
    expect(text).toContain('GH')
  })

  it('applies flat class when active is true (no nested box)', () => {
    const { container } = render(PoolStatsCard, { props: { stat: basestat, active: true } })
    expect(container.querySelector('.pool-stats-card')).toHaveClass('flat')
  })

  it('does not apply flat class when active is false', () => {
    const { container } = render(PoolStatsCard, { props: { stat: basestat, active: false } })
    expect(container.querySelector('.pool-stats-card')).not.toHaveClass('flat')
  })

  it('renders timestamp sub-line for best diff and blocks', () => {
    const stat = {
      ...basestat,
      blocks_found: 1,
      best_diff_ts: Math.floor(Date.now() / 1000) - 120, // 2m ago
      last_block_ts: Math.floor(Date.now() / 1000) - 3600, // 1h ago
    }
    const { container } = render(PoolStatsCard, { props: { stat, active: false } })
    const subs = container.querySelectorAll('.stat-sub')
    expect(subs.length).toBe(2)
    expect(subs[0].textContent).toMatch(/m ago/)
    expect(subs[1].textContent).toMatch(/h ago/)
  })

  it('shows em-dash for unset timestamps', () => {
    const { container } = render(PoolStatsCard, {
      props: { stat: { ...basestat, best_diff_ts: 0, last_block_ts: 0 }, active: false },
    })
    const subs = container.querySelectorAll('.stat-sub')
    expect(subs[0].textContent).toBe('—')
    expect(subs[1].textContent).toBe('—')
  })
})
