import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import { rebooting } from '../lib/stores'

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

import RebootOverlay from './RebootOverlay.svelte'

describe('RebootOverlay', () => {
  beforeEach(() => {
    rebooting.set({ active: false, reason: '', elapsed: 0, timedOut: false })
  })

  it('does not render when rebooting.active=false', () => {
    render(RebootOverlay)
    expect(document.querySelector('.reboot-backdrop')).toBeNull()
  })

  it('renders spinner overlay when rebooting.active=true', () => {
    rebooting.set({ active: true, reason: 'Rebooting…', elapsed: 5, timedOut: false })
    render(RebootOverlay)
    expect(document.querySelector('.reboot-backdrop')).not.toBeNull()
  })

  it('shows reason text during active reboot', () => {
    rebooting.set({ active: true, reason: 'Applying update', elapsed: 3, timedOut: false })
    render(RebootOverlay)
    expect(screen.getByText('Applying update')).toBeInTheDocument()
  })

  it('shows elapsed time', () => {
    rebooting.set({ active: true, reason: 'Rebooting', elapsed: 12, timedOut: false })
    render(RebootOverlay)
    expect(screen.getByText(/12s/)).toBeInTheDocument()
  })

  it('shows timeout message when timedOut=true', () => {
    rebooting.set({ active: true, reason: 'a reboot', elapsed: 91, timedOut: true })
    render(RebootOverlay)
    expect(screen.getByText('Miner not responding')).toBeInTheDocument()
    expect(screen.getByText(/stuck during boot/)).toBeInTheDocument()
  })

  it('has role=alert for accessibility', () => {
    rebooting.set({ active: true, reason: 'Rebooting', elapsed: 1, timedOut: false })
    render(RebootOverlay)
    expect(document.querySelector('[role="alert"]')).not.toBeNull()
  })
})
