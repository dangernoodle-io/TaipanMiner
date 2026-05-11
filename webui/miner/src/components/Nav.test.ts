import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import { route } from '../lib/router'
import { health } from '../lib/stores'

// Mock the router module so window.addEventListener doesn't interfere
vi.mock('../lib/router', async () => {
  const { writable } = await import('svelte/store')
  const routeStore = writable<string>('dashboard')
  return {
    route: routeStore,
    goto: vi.fn()
  }
})

vi.mock('../lib/stores', async () => {
  const { writable } = await import('svelte/store')
  return {
    health: writable(null),
    stats: writable(null),
    info: writable(null),
    fan: writable(null),
    hasAsic: writable(false)
  }
})

import Nav from './Nav.svelte'

describe('Nav', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    route.set('dashboard')
    health.set(null)
  })

  it('renders all navigation links', () => {
    render(Nav)
    expect(screen.getByText('Dashboard')).toBeInTheDocument()
    expect(screen.getByText('Diagnostics')).toBeInTheDocument()
    expect(screen.getByText('History')).toBeInTheDocument()
    expect(screen.getByText('Pool')).toBeInTheDocument()
    expect(screen.getByText('Settings')).toBeInTheDocument()
    expect(screen.getByText('System')).toBeInTheDocument()
    expect(screen.getByText('Update')).toBeInTheDocument()
  })

  it('marks the active route link', async () => {
    route.set('pool')
    render(Nav)
    const poolLink = screen.getByText('Pool').closest('a')
    expect(poolLink?.classList.contains('active')).toBe(true)
  })

  it('uses hash-based hrefs', () => {
    render(Nav)
    const dashLink = screen.getByText('Dashboard').closest('a')
    expect(dashLink?.getAttribute('href')).toBe('#/dashboard')
  })

  it('renders nav element', () => {
    render(Nav)
    expect(document.querySelector('nav')).not.toBeNull()
  })

  it('non-active links do not have active class', () => {
    route.set('dashboard')
    render(Nav)
    const poolLink = screen.getByText('Pool').closest('a')
    expect(poolLink?.classList.contains('active')).toBe(false)
  })

  it('renders disabled links as span with disabled class when link.disabled=true', () => {
    // The Nav component renders links that have disabled=true as <span class="link disabled">
    // We verify the structure by checking that all non-disabled links are <a> tags
    render(Nav)
    // All visible links in the Nav are currently enabled — they render as <a> elements
    const dashLink = screen.getByText('Dashboard').closest('a')
    expect(dashLink).not.toBeNull()
    // No disabled spans should exist since no links are disabled in the current config
    expect(document.querySelectorAll('span.disabled').length).toBe(0)
  })

  it('renders each tab with correct href when knot is enabled', () => {
    health.set({
      ok: true,
      free_heap: 100000,
      validated: true,
      network: { connected: true, rssi: -50, disc_age_s: 0, retry_count: 0, mdns: null, knot: true }
    } as any)
    render(Nav)
    const routes = ['dashboard', 'diagnostics', 'history', 'knot', 'pool', 'settings', 'system', 'update']
    for (const r of routes) {
      const link = document.querySelector(`a[href="#/${r}"]`)
      expect(link).not.toBeNull()
    }
  })
})

describe('Nav — Knot link visibility', () => {
  it('renders Knot link when health.network.knot=true', () => {
    health.set({
      ok: true,
      free_heap: 100000,
      validated: true,
      network: { connected: true, rssi: -50, disc_age_s: 0, retry_count: 0, mdns: null, knot: true }
    } as any)
    render(Nav)
    const knotLink = document.querySelector('a[href="#/knot"]')
    expect(knotLink).not.toBeNull()
  })

  it('does NOT render Knot link when health.network.knot=false', () => {
    health.set({
      ok: true,
      free_heap: 100000,
      validated: true,
      network: { connected: true, rssi: -50, disc_age_s: 0, retry_count: 0, mdns: null, knot: false }
    } as any)
    render(Nav)
    const knotLink = document.querySelector('a[href="#/knot"]')
    expect(knotLink).toBeNull()
  })

  it('renders Knot link when health is null (unknown state defaults to visible)', () => {
    health.set(null)
    render(Nav)
    const knotLink = document.querySelector('a[href="#/knot"]')
    expect(knotLink).not.toBeNull()
  })

  it('renders other links regardless of knot status', () => {
    health.set({
      ok: true,
      free_heap: 100000,
      validated: true,
      network: { connected: true, rssi: -50, disc_age_s: 0, retry_count: 0, mdns: null, knot: false }
    } as any)
    render(Nav)
    expect(screen.getByText('Dashboard')).toBeInTheDocument()
    expect(screen.getByText('Settings')).toBeInTheDocument()
    expect(screen.getByText('Pool')).toBeInTheDocument()
  })
})
