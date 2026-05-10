import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import { info } from '../lib/stores'

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

// Brand imports logo.svg?raw which the test runner can't resolve via package exports.
// Mock the whole Brand component with a minimal pass-through that renders the title prop.
vi.mock('ui-kit/Brand.svelte', () => {
  // Return a minimal Svelte 5 component object that @testing-library/svelte can mount.
  // We use the same shape that the Svelte compiler produces: a function-based component.
  const Brand = (anchor: any, props: any) => {
    const el = document.createElement('header')
    el.className = 'brand'
    const h1 = document.createElement('h1')
    h1.textContent = props.title ?? ''
    el.appendChild(h1)
    // render children snippet if present
    if (props.children) {
      try {
        const slot = document.createElement('div')
        el.appendChild(slot)
        props.children(slot)
      } catch { /* noop */ }
    }
    anchor.before(el)
    return { destroy() { el.remove() } }
  }
  Brand.render = () => ({ html: '', head: '', css: { code: '', map: null } })
  return { default: Brand }
})

import Header from './Header.svelte'

describe('Header', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    info.set(null)
  })

  it('renders without crashing when info is null', () => {
    const { container } = render(Header)
    expect(container).toBeTruthy()
  })

  it('shows board and version when info is set', () => {
    info.set({
      board: 'bitaxe-601',
      version: '1.2.3',
      hostname: 'miner',
      ip: '192.168.1.1',
      mac: 'AA:BB:CC:DD:EE:FF',
      idf_version: '5.5',
      build_date: 'Jan  1 2025',
      build_time: '00:00:00',
      reset_reason: 'Power on',
      wdt_resets: 0,
      uptime_boot_s: 0
    } as any)
    render(Header)
    expect(screen.getByText('bitaxe-601 · 1.2.3')).toBeInTheDocument()
  })
})
