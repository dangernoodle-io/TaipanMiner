import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import { route } from '../lib/router'

// Mock the router module so window.addEventListener doesn't interfere
vi.mock('../lib/router', async (importOriginal) => {
  const { writable } = await import('svelte/store')
  const routeStore = writable<string>('dashboard')
  return {
    route: routeStore,
    goto: vi.fn()
  }
})

import Nav from './Nav.svelte'

describe('Nav', () => {
  beforeEach(() => {
    route.set('dashboard')
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
})
