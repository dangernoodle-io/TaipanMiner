import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render } from '@testing-library/svelte'

vi.mock('../lib/api', () => ({
  fetchScan: vi.fn(),
  fetchVersion: vi.fn(),
  postSave: vi.fn(),
}))

import { fetchScan, postSave } from '../lib/api'
import WifiSetup from './WifiSetup.svelte'

afterEach(() => {
  vi.clearAllMocks()
})

describe('WifiSetup', () => {
  const mockOnSaved = vi.fn()

  beforeEach(() => {
    vi.clearAllMocks()
    ;(fetchScan as ReturnType<typeof vi.fn>).mockResolvedValue([
      { ssid: 'TestNet1', rssi: -50, secure: true },
      { ssid: 'TestNet2', rssi: -70, secure: false }
    ])
  })

  it('renders without crashing', () => {
    const result = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    expect(result.component).toBeDefined()
  })

  it('displays form sections', () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const sections = container.querySelectorAll('section')
    expect(sections.length).toBeGreaterThan(0)
  })

  it('calls fetchScan on mount', async () => {
    render(WifiSetup, { props: { onSaved: mockOnSaved } })
    await new Promise(resolve => setTimeout(resolve, 100))
    expect(fetchScan).toHaveBeenCalled()
  })

  it('renders form inputs', async () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    await new Promise(resolve => setTimeout(resolve, 100))
    const inputs = container.querySelectorAll('input')
    expect(inputs.length).toBeGreaterThan(0)
  })

  it('renders submit button', () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const submitBtn = container.querySelector('button[type="submit"]')
    expect(submitBtn).toBeTruthy()
  })

  it('has proper form structure', () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const form = container.querySelector('form')
    expect(form).toBeTruthy()
  })

  it('shows rescan button', () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const rescanBtn = container.querySelector('button[aria-label="Rescan networks"]')
    expect(rescanBtn).toBeTruthy()
  })

  it('shows password input', () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const passInput = container.querySelector('#pass') as HTMLInputElement
    expect(passInput).toBeTruthy()
    expect(passInput.type).toBe('password')
  })

  it('shows hostname input', () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const hostnameInput = container.querySelector('#hostname') as HTMLInputElement
    expect(hostnameInput).toBeTruthy()
  })

  it('shows wallet input', () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const walletInput = container.querySelector('#wallet') as HTMLInputElement
    expect(walletInput).toBeTruthy()
  })

  it('shows worker input', () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const workerInput = container.querySelector('#worker') as HTMLInputElement
    expect(workerInput).toBeTruthy()
  })

  it('shows pool host input', () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const poolHostInput = container.querySelector('#pool_host') as HTMLInputElement
    expect(poolHostInput).toBeTruthy()
  })

  it('shows pool port input', () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const poolPortInput = container.querySelector('#pool_port') as HTMLInputElement
    expect(poolPortInput).toBeTruthy()
  })

  it('shows pool password input', () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const poolPassInput = container.querySelector('#pool_pass') as HTMLInputElement
    expect(poolPassInput).toBeTruthy()
  })

  it('renders setup form container', async () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const setupForm = container.querySelector('.setup-form')
    expect(setupForm).toBeTruthy()
  })

  it('displays scan error when fetchScan fails', async () => {
    ;(fetchScan as ReturnType<typeof vi.fn>).mockRejectedValueOnce(new Error('Scan error'))
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    await new Promise(resolve => setTimeout(resolve, 100))
    const inlineError = container.querySelector('.inline-error')
    expect(inlineError).toBeTruthy()
  })

  it('renders sections with correct headings', () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const headings = Array.from(container.querySelectorAll('h2'))
    const headingTexts = headings.map(h => h.textContent?.trim())
    expect(headingTexts).toContain('WiFi')
    expect(headingTexts).toContain('Mining')
    expect(headingTexts).toContain('Pool')
  })

  it('has correct form structure with form element', () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const form = container.querySelector('form')
    expect(form).toBeTruthy()
    const formGroups = form?.querySelectorAll('.form-group') || []
    expect(formGroups.length).toBeGreaterThan(0)
  })

  it('renders password toggle button', () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const toggleBtn = container.querySelector('.toggle-pass')
    expect(toggleBtn).toBeTruthy()
  })

  it('shows scan-controls wrapper', () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const scanControls = container.querySelector('.scan-controls')
    expect(scanControls).toBeTruthy()
  })

  it('has proper label attributes', () => {
    const { container } = render(WifiSetup, { props: { onSaved: mockOnSaved } })
    const labels = Array.from(container.querySelectorAll('label'))
    const labelTexts = labels.map(l => l.textContent?.trim().toUpperCase())
    expect(labelTexts.length).toBeGreaterThan(0)
  })
})
