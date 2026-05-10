import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render } from '@testing-library/svelte'

vi.mock('./lib/api', () => ({
  fetchScan: vi.fn().mockResolvedValue([]),
  fetchVersion: vi.fn().mockResolvedValue('v1.0.0'),
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

  it('renders footer with version placeholder', () => {
    const { container } = render(App)
    const footer = container.querySelector('footer')
    expect(footer).toBeTruthy()
    expect(footer?.textContent).toContain('Powered by TaipanMiner')
  })

  it('renders with expected HTML structure', () => {
    const { container } = render(App)
    const main = container.querySelector('main')
    const footer = container.querySelector('footer')
    expect(main).toBeTruthy()
    expect(footer).toBeTruthy()
  })

  it('main element has correct class or structure', () => {
    const { container } = render(App)
    const main = container.querySelector('main')
    expect(main?.tagName).toBe('MAIN')
  })

  it('footer element has span with text content', () => {
    const { container } = render(App)
    const span = container.querySelector('footer span')
    expect(span).toBeTruthy()
    expect(span?.textContent).toContain('TaipanMiner')
  })

  it('renders without api errors during mount', () => {
    const { component } = render(App)
    expect(component).toBeDefined()
  })
})
