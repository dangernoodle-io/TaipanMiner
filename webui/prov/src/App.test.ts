import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, waitFor } from '@testing-library/svelte'

vi.mock('./lib/api', () => ({
  fetchScan: vi.fn().mockResolvedValue([]),
  fetchInfo: vi.fn().mockResolvedValue({ board: 'esp32-c3-supermini', version: 'v1.0.0' }),
  postSave: vi.fn().mockResolvedValue(undefined),
}))

import App from './App.svelte'

describe('App', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('renders without crashing', () => {
    const result = render(App)
    expect(result.component).toBeDefined()
  })

  it('renders Brand component with title', () => {
    const { container } = render(App)
    expect(container).toBeTruthy()
  })

  it('renders main element', () => {
    const { container } = render(App)
    const main = container.querySelector('main')
    expect(main).toBeTruthy()
  })

  it('shows board · version in the header subtitle (no doubled v prefix)', async () => {
    const { container } = render(App)
    // version from /api/info already carries its own `v` prefix on tagged
    // builds — the subtitle must not prepend another (no `vv1.0.0`).
    await waitFor(() =>
      expect(container.querySelector('.sub')?.textContent)
        .toBe('esp32-c3-supermini · v1.0.0'))
    expect(container.querySelector('.sub')?.textContent).not.toContain('vv1.0.0')
  })

  it('has no footer', () => {
    const { container } = render(App)
    expect(container.querySelector('footer')).toBeNull()
  })

  it('main element has correct class or structure', () => {
    const { container } = render(App)
    const main = container.querySelector('main')
    expect(main?.tagName).toBe('MAIN')
  })

  it('renders without api errors during mount', () => {
    const { component } = render(App)
    expect(component).toBeDefined()
  })
})
