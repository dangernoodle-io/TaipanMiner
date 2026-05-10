import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render } from '@testing-library/svelte'

vi.mock('../lib/api', () => ({
  fetchStats: vi.fn(), fetchInfo: vi.fn(), fetchPower: vi.fn(), fetchFan: vi.fn(),
  fetchSettings: vi.fn(), fetchPool: vi.fn(), fetchHealth: vi.fn(), ping: vi.fn()
}))

// Use vi.hoisted() so stubHs is available at module-evaluation time (avoids TDZ).
const stubHs = vi.hoisted(() => ({
  windowIdx: 2,
  count: 0,
  metrics: [],
  plot: null,
  WINDOWS: [
    { label: '1m',  seconds: 60 },
    { label: '5m',  seconds: 300 },
    { label: '15m', seconds: 900 },
    { label: '1h',  seconds: 3600 },
    { label: 'All', seconds: 0 }
  ],
  selectWindow: vi.fn(),
  mountChart: vi.fn(),
  destroyChart: vi.fn(),
}))

vi.mock('../lib/historyState.svelte', () => ({
  createHistoryState: () => stubHs
}))

import History from './History.svelte'

beforeEach(() => {
  vi.clearAllMocks()
  stubHs.windowIdx = 2
  stubHs.count = 0
})

describe('History (thin shell)', () => {
  it('renders without crashing', () => {
    const { component } = render(History)
    expect(component).toBeDefined()
  })

  it('shows empty state when count is 0', () => {
    stubHs.count = 0
    const { container } = render(History)
    expect(container.querySelector('.empty')).not.toBeNull()
  })

  it('does not show chart div when count is 0', () => {
    stubHs.count = 0
    const { container } = render(History)
    expect(container.querySelector('.chart')).toBeNull()
  })

  it('shows chart div when count > 0', () => {
    stubHs.count = 10
    const { container } = render(History)
    expect(container.querySelector('.chart')).not.toBeNull()
  })

  it('shows hint text when count > 0', () => {
    stubHs.count = 5
    const { container } = render(History)
    const hint = container.querySelector('.hint')
    expect(hint).not.toBeNull()
    expect(hint!.textContent).toContain('5 samples')
  })

  it('renders all 5 window buttons', () => {
    const { container } = render(History)
    const buttons = container.querySelectorAll('.win-btn')
    expect(buttons).toHaveLength(5)
  })

  it('window button labels match WINDOWS', () => {
    const { container } = render(History)
    const buttons = container.querySelectorAll('.win-btn')
    const labels = Array.from(buttons).map((b) => b.textContent?.trim())
    expect(labels).toEqual(['1m', '5m', '15m', '1h', 'All'])
  })

  it('active class is on the correct window button', () => {
    stubHs.windowIdx = 1
    const { container } = render(History)
    const buttons = container.querySelectorAll('.win-btn')
    expect(buttons[1].classList).toContain('active')
    expect(buttons[0].classList).not.toContain('active')
  })

  it('active class tracks windowIdx=0', () => {
    stubHs.windowIdx = 0
    const { container } = render(History)
    const buttons = container.querySelectorAll('.win-btn')
    expect(buttons[0].classList).toContain('active')
  })

  it('calls mountChart on mount', () => {
    render(History)
    expect(stubHs.mountChart).toHaveBeenCalledTimes(1)
  })

  it('calls destroyChart on unmount', () => {
    const { unmount } = render(History)
    unmount()
    expect(stubHs.destroyChart).toHaveBeenCalledTimes(1)
  })

  it('calls selectWindow when a window button is clicked', async () => {
    const { container } = render(History)
    const buttons = container.querySelectorAll('.win-btn')
    ;(buttons[0] as HTMLButtonElement).click()
    expect(stubHs.selectWindow).toHaveBeenCalledWith(0)
  })

  it('calls selectWindow(4) when All button is clicked', async () => {
    const { container } = render(History)
    const buttons = container.querySelectorAll('.win-btn')
    ;(buttons[4] as HTMLButtonElement).click()
    expect(stubHs.selectWindow).toHaveBeenCalledWith(4)
  })

  it('toolbar is present', () => {
    const { container } = render(History)
    expect(container.querySelector('.toolbar')).not.toBeNull()
  })

  it('windows container is present', () => {
    const { container } = render(History)
    expect(container.querySelector('.windows')).not.toBeNull()
  })
})
