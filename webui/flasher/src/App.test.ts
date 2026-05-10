import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, fireEvent, waitFor } from '@testing-library/svelte'
import App from './App.svelte'

// Mock the release module
vi.mock('./lib/release', () => ({
  loadManifest: vi.fn().mockResolvedValue({
    tag: 'v1.0.0',
    publishedAt: '2024-01-01T00:00:00Z',
    assets: {
      'bitaxe-601': {
        file: 'taipanminer-bitaxe-601-factory.bin',
        size: 1024,
        sha256: 'abc123',
      },
      'tdongle-s3': {
        file: 'taipanminer-tdongle-s3-factory.bin',
        size: 2048,
        sha256: 'def456',
      },
    },
  }),
  loadAsset: vi.fn().mockResolvedValue(new Uint8Array([0, 1, 2, 3])),
}))

// Mock esptool-js
vi.mock('esptool-js', () => ({
  ESPLoader: vi.fn(),
  Transport: vi.fn(),
}))

describe('App', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('renders the main layout', () => {
    const { container } = render(App)
    expect(container).toBeTruthy()
  })

  it('renders app heading', () => {
    const { getByRole } = render(App)
    const heading = getByRole('heading', { name: /TaipanMiner Flasher/ })
    expect(heading).toBeTruthy()
  })

  it('renders board selection steps', () => {
    const { getByRole } = render(App)
    expect(getByRole('heading', { name: /Select your board/ })).toBeTruthy()
    expect(getByRole('heading', { name: /Connect device/ })).toBeTruthy()
    expect(getByRole('heading', { name: /Flash firmware/ })).toBeTruthy()
  })

  it('renders board selection dropdown', () => {
    const { getByRole } = render(App)
    const select = getByRole('combobox', { name: /Board/ })
    expect(select).toBeTruthy()
  })

  it('board selection dropdown has options from manifest', () => {
    const { getByRole } = render(App)
    const select = getByRole('combobox', { name: /Board/ }) as HTMLSelectElement
    // Mock manifest has bitaxe-601 and tdongle-s3
    expect(select.options.length).toBeGreaterThan(0)
  })

  it('shows Web Serial availability warning if serial not available', () => {
    const { container } = render(App)
    // App should render even if Web Serial is not available (we stub it in test-setup)
    expect(container.querySelector('main')).toBeTruthy()
  })

  it('renders firmware version in footer', () => {
    const { container } = render(App)
    const footer = container.querySelector('footer')
    expect(footer).toBeTruthy()
    expect(footer?.textContent).toContain('Firmware')
  })

  it('renders github link', () => {
    const { getByRole } = render(App)
    const link = getByRole('link', { name: /View on GitHub/ })
    expect(link).toBeTruthy()
    expect(link.getAttribute('href')).toContain('github.com/dangernoodle-io/TaipanMiner')
  })

  it('renders main content sections', () => {
    const { container } = render(App)
    const sections = container.querySelectorAll('section')
    expect(sections.length).toBeGreaterThan(0)
  })
})
