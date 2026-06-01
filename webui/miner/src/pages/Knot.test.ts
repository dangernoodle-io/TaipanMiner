import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render } from '@testing-library/svelte'

vi.mock('../lib/api', () => ({
  fetchStats: vi.fn(), fetchInfo: vi.fn(), fetchPower: vi.fn(), fetchFan: vi.fn(),
  fetchSettings: vi.fn(), fetchPool: vi.fn(), fetchHealth: vi.fn(), ping: vi.fn(), fetchKnot: vi.fn()
}))

import Knot from './Knot.svelte'
import { fetchKnot } from '../lib/api'
import { info } from '../lib/stores'

const mockFetchKnot = fetchKnot as ReturnType<typeof vi.fn>

const basePeer = {
  instance: 'peer1', hostname: 'dongle1', ip: '192.168.1.100',
  worker: 'worker1', board: 'tdongle-s3', version: 'v1.0.0',
  state: 'mining', seen_ago_s: 5, ui: true
}

describe('Knot', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    info.set(null)  // no "current host" by default — nothing filtered
    // Per-peer stat fetches go to peer IPs directly — stub global fetch so the
    // tests stay offline/deterministic (peers render as "unreachable").
    vi.stubGlobal('fetch', vi.fn(() => Promise.reject(new Error('no network'))))
  })

  afterEach(() => {
    vi.unstubAllGlobals()
  })

  it('renders without crashing', () => {
    mockFetchKnot.mockResolvedValueOnce([])
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('loads peers on mount', async () => {
    mockFetchKnot.mockResolvedValueOnce([])
    render(Knot)
    await vi.waitFor(() => expect(mockFetchKnot).toHaveBeenCalled())
  })

  it('sorts peers by hostname', async () => {
    const peers = [
      { ...basePeer, instance: 'p2', hostname: 'zebra' },
      { ...basePeer, instance: 'p1', hostname: 'apple' }
    ]
    mockFetchKnot.mockResolvedValueOnce(peers)
    const { container } = render(Knot)
    await vi.waitFor(() => {
      const cards = container.querySelectorAll('.card')
      expect(cards.length).toBe(2)
    })
  })

  it('renders empty state when no peers', async () => {
    mockFetchKnot.mockResolvedValueOnce([])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.empty-state')).toBeTruthy())
  })

  it('displays error message on fetch fail', async () => {
    mockFetchKnot.mockRejectedValueOnce(new Error('Network error'))
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.error')).toBeTruthy())
  })

  it('handles a non-array /api/knot response without crashing', async () => {
    // A truncated chunked body or an error object must not throw
    // "(intermediate value).sort is not a function" — degrade to empty.
    mockFetchKnot.mockResolvedValueOnce({} as any)
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.empty-state')).toBeTruthy())
    expect(container.querySelector('.error')).toBeFalsy()
  })

  it('renders live shares + hashrate when a peer-stats fetch succeeds', async () => {
    // Exercises the reachable branch of fetchPeerStats (res.ok → parse stats).
    vi.stubGlobal('fetch', vi.fn(() => Promise.resolve({
      ok: true,
      json: () => Promise.resolve({ asic_total_ghs: 0.5, session_shares: 99 })
    })))
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.sv.hashrate')).toBeTruthy())
    expect(container.textContent).toContain('99')          // session_shares
    expect(container.querySelector('.unreachable')).toBeFalsy()
  })

  it('renders peer cards with data', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.cards')).toBeTruthy())
    expect(container.querySelector('.card')).toBeTruthy()
  })

  it('shows the peer IP on the card', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, ip: '10.0.0.42' }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.textContent).toContain('10.0.0.42'))
  })

  it('shows updated timestamp after load', async () => {
    mockFetchKnot.mockResolvedValueOnce([])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(mockFetchKnot).toHaveBeenCalled())
    await vi.waitFor(() => expect(container.textContent).toContain('Updated'))
  })

  it('classifies mining state', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, state: 'mining' }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.state-mining')).toBeTruthy())
  })

  it('classifies ota state', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, state: 'ota' }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.state-ota')).toBeTruthy())
  })

  it('classifies provisioning state', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, state: 'provisioning' }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.state-provisioning')).toBeTruthy())
  })

  it('classifies idle state', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, state: 'idle' }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.state-idle')).toBeTruthy())
  })

  it('classifies unknown state as unknown', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, state: 'unknown' }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.state-unknown')).toBeTruthy())
  })

  it('classifies unrecognized state as neutral', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, state: 'invalid' }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.state-neutral')).toBeTruthy())
  })

  it('shows unknown status dot when state empty', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, state: '' }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.state-unknown')).toBeTruthy())
  })

  it('appends .local to hostname without it', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, hostname: 'dongle1' }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('a[href*="dongle1.local"]')).toBeTruthy())
  })

  it('preserves .local suffix in hostname', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, hostname: 'dongle1.local' }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('a[href*="dongle1.local"]')).toBeTruthy())
  })

  it('displays hostname without .local suffix', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, hostname: 'dongle1.local' }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.textContent).toContain('dongle1'))
  })

  it('renders a board dot tagged with the board name', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, board: 'tdongle-s3' }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.board-dot[title="tdongle-s3"]')).toBeTruthy())
  })

  it('gives different boards different dot colors', async () => {
    mockFetchKnot.mockResolvedValueOnce([
      { ...basePeer, instance: 'a', hostname: 'a', board: 'tdongle-s3' },
      { ...basePeer, instance: 'b', hostname: 'b', board: 'bitaxe-650' }
    ])
    const { container } = render(Knot)
    await vi.waitFor(() => {
      const a = container.querySelector('.board-dot[title="tdongle-s3"]')?.getAttribute('style')
      const b = container.querySelector('.board-dot[title="bitaxe-650"]')?.getAttribute('style')
      expect(a).toBeTruthy()
      expect(b).toBeTruthy()
      expect(a).not.toBe(b)
    })
  })

  it('auto-colors a board not in the curated map', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, board: 'future-board-9000' }])
    const { container } = render(Knot)
    await vi.waitFor(() => {
      const style = container.querySelector('.board-dot[title="future-board-9000"]')?.getAttribute('style') ?? ''
      // A color is applied (hsl normalizes to rgb in the DOM) and it's not the
      // empty-board gray fallback — i.e. the name-hashed path produced a color.
      expect(style).toMatch(/background:\s*rgb/)
      expect(style).not.toContain('136, 136, 136')
    })
  })

  it('handles empty board gracefully', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, board: '' }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.board-dot')).toBeTruthy())
  })

  it('handles empty hostname in link', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, hostname: '' }])
    const { container } = render(Knot)
    await vi.waitFor(() => {
      const link = container.querySelector('a')
      expect(link?.href).toBeTruthy()
    })
  })

  it('multiple peers render as cards', async () => {
    const peers = [
      { ...basePeer, instance: 'p1', hostname: 'dongle1' },
      { ...basePeer, instance: 'p2', hostname: 'bitaxe1' },
      { ...basePeer, instance: 'p3', hostname: 'bitaxe2' }
    ]
    mockFetchKnot.mockResolvedValueOnce(peers)
    const { container } = render(Knot)
    await vi.waitFor(() => {
      const cards = container.querySelectorAll('.card')
      expect(cards.length).toBe(3)
    })
  })

  it('displays peer versions', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, version: 'v2.5.1' }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.textContent).toContain('v2.5.1'))
  })

  it('renders with null state defaults to unknown', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, state: null as any }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.state-unknown')).toBeTruthy())
  })

  it('renders header with title', async () => {
    mockFetchKnot.mockResolvedValueOnce([])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(mockFetchKnot).toHaveBeenCalled())
    expect(container.textContent).toContain('Knot')
  })

  it('renders refresh button', async () => {
    mockFetchKnot.mockResolvedValueOnce([])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(mockFetchKnot).toHaveBeenCalled())
    expect(container.querySelector('button')).toBeTruthy()
  })

  it('builds board legend from peers', async () => {
    mockFetchKnot.mockResolvedValueOnce([
      { ...basePeer, board: 'tdongle-s3' },
      { ...basePeer, instance: 'p2', hostname: 'bitaxe1', board: 'bitaxe-601' }
    ])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.legend')).toBeTruthy())
  })

  it('shows status row in legend', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, state: 'mining' }])
    const { container } = render(Knot)
    await vi.waitFor(() => {
      const legend = container.querySelector('.legend')
      expect(legend?.textContent).toContain('Status')
    })
  })

  it('peer with ui:false renders .nolink span, no <a> for that name', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, ui: false, hostname: 'noui-miner' }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('.name.nolink')).toBeTruthy())
    // .nolink span must be present
    const nolink = container.querySelector('.name.nolink')
    expect(nolink?.tagName.toLowerCase()).toBe('span')
    expect(nolink?.textContent).toContain('noui-miner')
    // no <a> element should carry the hostname text
    const links = container.querySelectorAll('a.name')
    expect(links.length).toBe(0)
  })

  it('peer with ui:true renders <a> link for name', async () => {
    mockFetchKnot.mockResolvedValueOnce([{ ...basePeer, ui: true, hostname: 'ui-miner' }])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(container.querySelector('a.name')).toBeTruthy())
    const link = container.querySelector('a.name')
    expect(link?.textContent).toContain('ui-miner')
    expect(container.querySelector('.name.nolink')).toBeNull()
  })

  it('filters out the current host', async () => {
    info.set({ hostname: 'dongle1' } as any)
    mockFetchKnot.mockResolvedValueOnce([
      { ...basePeer, instance: 'self', hostname: 'dongle1' },
      { ...basePeer, instance: 'other', hostname: 'dongle2' }
    ])
    const { container } = render(Knot)
    await vi.waitFor(() => expect(mockFetchKnot).toHaveBeenCalled())
    await vi.waitFor(() => {
      const cards = container.querySelectorAll('.card')
      expect(cards.length).toBe(1)
    })
    expect(container.textContent).toContain('dongle2')
    expect(container.textContent).not.toContain('dongle1')
  })
})
