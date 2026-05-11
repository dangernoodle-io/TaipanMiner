import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, fireEvent } from '@testing-library/svelte'

// Mock createWifiSetupState so the component renders with a stub state
vi.mock('../lib/wifiSetupState.svelte', () => ({
  createWifiSetupState: vi.fn(),
}))

import { createWifiSetupState } from '../lib/wifiSetupState.svelte'
import WifiSetup from './WifiSetup.svelte'

function makeStubState(overrides: Record<string, unknown> = {}) {
  return {
    networks: [],
    scanning: false,
    scanError: null,
    selectedSsid: '',
    manualSsid: '',
    pass: '',
    showPass: false,
    hostname: '',
    wallet: '',
    worker: '',
    poolHost: '',
    poolPort: '',
    poolPass: '',
    errors: {},
    submitting: false,
    submitError: null,
    scan: vi.fn(),
    validate: vi.fn().mockReturnValue(true),
    handleSubmit: vi.fn(),
    ...overrides,
  }
}

beforeEach(() => {
  vi.clearAllMocks()
  vi.mocked(createWifiSetupState).mockReturnValue(makeStubState() as ReturnType<typeof makeStubState>)
})

const onSaved = vi.fn()
const mount = (stateOverrides: Record<string, unknown> = {}) => {
  vi.mocked(createWifiSetupState).mockReturnValue(makeStubState(stateOverrides) as ReturnType<typeof makeStubState>)
  return render(WifiSetup, { props: { onSaved } })
}

describe('WifiSetup — structure', () => {
  it('renders without crashing', () => {
    const { container } = mount()
    expect(container.querySelector('.setup-form')).toBeTruthy()
  })

  it('renders WiFi, Mining, Pool sections', () => {
    const { container } = mount()
    const headings = Array.from(container.querySelectorAll('h2')).map(h => h.textContent?.trim())
    expect(headings).toContain('WiFi')
    expect(headings).toContain('Mining')
    expect(headings).toContain('Pool')
  })

  it('renders a form element', () => {
    const { container } = mount()
    expect(container.querySelector('form')).toBeTruthy()
  })

  it('renders submit button', () => {
    const { container } = mount()
    expect(container.querySelector('button[type="submit"]')).toBeTruthy()
  })

  it('renders rescan button', () => {
    const { container } = mount()
    expect(container.querySelector('button[aria-label="Rescan networks"]')).toBeTruthy()
  })

  it('renders password input', () => {
    const { container } = mount()
    const passInput = container.querySelector('#pass') as HTMLInputElement
    expect(passInput).toBeTruthy()
    expect(passInput.type).toBe('password')
  })

  it('renders hostname, wallet, worker inputs', () => {
    const { container } = mount()
    expect(container.querySelector('#hostname')).toBeTruthy()
    expect(container.querySelector('#wallet')).toBeTruthy()
    expect(container.querySelector('#worker')).toBeTruthy()
  })

  it('renders pool host, port, password inputs', () => {
    const { container } = mount()
    expect(container.querySelector('#pool_host')).toBeTruthy()
    expect(container.querySelector('#pool_port')).toBeTruthy()
    expect(container.querySelector('#pool_pass')).toBeTruthy()
  })

  it('renders scan-controls wrapper', () => {
    const { container } = mount()
    expect(container.querySelector('.scan-controls')).toBeTruthy()
  })

  it('renders password toggle button', () => {
    const { container } = mount()
    expect(container.querySelector('.toggle-pass')).toBeTruthy()
  })
})

describe('WifiSetup — submit button state', () => {
  it('submit button is enabled when not submitting and not scanning', () => {
    const { container } = mount({ submitting: false, scanning: false })
    const btn = container.querySelector('button[type="submit"]') as HTMLButtonElement
    expect(btn.disabled).toBe(false)
  })

  it('submit button is disabled when submitting', () => {
    const { container } = mount({ submitting: true })
    const btn = container.querySelector('button[type="submit"]') as HTMLButtonElement
    expect(btn.disabled).toBe(true)
  })

  it('submit button is disabled when scanning', () => {
    const { container } = mount({ scanning: true })
    const btn = container.querySelector('button[type="submit"]') as HTMLButtonElement
    expect(btn.disabled).toBe(true)
  })

  it('shows "Saving..." text when submitting', () => {
    const { container } = mount({ submitting: true })
    const btn = container.querySelector('button[type="submit"]')
    expect(btn?.textContent?.trim()).toBe('Saving...')
  })

  it('shows "Save & Connect" text when not submitting', () => {
    const { container } = mount({ submitting: false })
    const btn = container.querySelector('button[type="submit"]')
    expect(btn?.textContent?.trim()).toBe('Save & Connect')
  })
})

describe('WifiSetup — error banners', () => {
  it('shows submitError banner when present', () => {
    const { container } = mount({ submitError: 'Save failed: 500' })
    const banner = container.querySelector('.error-banner')
    expect(banner).toBeTruthy()
    expect(banner?.textContent?.trim()).toBe('Save failed: 500')
  })

  it('hides submitError banner when null', () => {
    const { container } = mount({ submitError: null })
    expect(container.querySelector('.error-banner')).toBeNull()
  })

  it('shows inline scan error when scanError present', () => {
    const { container } = mount({ scanError: 'Scan failed: timeout' })
    const inlineErr = container.querySelector('.inline-error')
    expect(inlineErr).toBeTruthy()
    expect(inlineErr?.textContent?.trim()).toBe('Scan failed: timeout')
  })

  it('hides inline scan error when null', () => {
    const { container } = mount({ scanError: null })
    expect(container.querySelector('.inline-error')).toBeNull()
  })
})

describe('WifiSetup — manual SSID entry', () => {
  it('shows manual entry input when selectedSsid is __manual__', () => {
    const { container } = mount({ selectedSsid: '__manual__' })
    expect(container.querySelector('.manual-entry')).toBeTruthy()
  })

  it('hides manual entry input otherwise', () => {
    const { container } = mount({ selectedSsid: 'HomeNet' })
    expect(container.querySelector('.manual-entry')).toBeNull()
  })

  it('hides manual entry when selectedSsid is empty', () => {
    const { container } = mount({ selectedSsid: '' })
    expect(container.querySelector('.manual-entry')).toBeNull()
  })
})

describe('WifiSetup — field error display', () => {
  it('shows wallet field error', () => {
    const { container } = mount({ errors: { wallet: 'Required' } })
    const fieldErrors = Array.from(container.querySelectorAll('.field-error'))
    expect(fieldErrors.some(e => e.textContent?.includes('Required'))).toBe(true)
  })

  it('shows poolPort field error', () => {
    const { container } = mount({ errors: { poolPort: 'Valid port (1–65535) required' } })
    const fieldErrors = Array.from(container.querySelectorAll('.field-error'))
    expect(fieldErrors.some(e => e.textContent?.includes('Valid port'))).toBe(true)
  })

  it('shows no field errors when errors is empty', () => {
    const { container } = mount({ errors: {} })
    expect(container.querySelectorAll('.field-error').length).toBe(0)
  })
})

describe('WifiSetup — password visibility', () => {
  it('password input is type=password when showPass is false', () => {
    const { container } = mount({ showPass: false })
    const input = container.querySelector('#pass') as HTMLInputElement
    expect(input.type).toBe('password')
  })

  it('password input is type=text when showPass is true', () => {
    const { container } = mount({ showPass: true })
    const input = container.querySelector('#pass') as HTMLInputElement
    expect(input.type).toBe('text')
  })
})

describe('WifiSetup — scan on mount', () => {
  it('calls scan on mount', () => {
    const scanFn = vi.fn()
    vi.mocked(createWifiSetupState).mockReturnValue(makeStubState({ scan: scanFn }) as ReturnType<typeof makeStubState>)
    render(WifiSetup, { props: { onSaved } })
    expect(scanFn).toHaveBeenCalledOnce()
  })
})

describe('WifiSetup — input handlers write back to state', () => {
  // Each form input uses value/oninput against the state machine. These
  // tests fire change events to exercise the inline arrow handlers; without
  // them coverage tools count the handler bodies as unhit dead code even
  // though they carry the real read-back behavior.
  it('manual SSID input updates ws.manualSsid', async () => {
    const stub = makeStubState({ selectedSsid: '__manual__' })
    vi.mocked(createWifiSetupState).mockReturnValue(stub as ReturnType<typeof makeStubState>)
    const { container } = render(WifiSetup, { props: { onSaved } })
    const input = container.querySelector('.manual-entry input') as HTMLInputElement
    await fireEvent.input(input, { target: { value: 'MyAP' } })
    expect(stub.manualSsid).toBe('MyAP')
  })

  it('password input updates ws.pass', async () => {
    const stub = makeStubState()
    vi.mocked(createWifiSetupState).mockReturnValue(stub as ReturnType<typeof makeStubState>)
    const { container } = render(WifiSetup, { props: { onSaved } })
    const input = container.querySelector('#pass') as HTMLInputElement
    await fireEvent.input(input, { target: { value: 'secret' } })
    expect(stub.pass).toBe('secret')
  })

  it('wallet input updates ws.wallet', async () => {
    const stub = makeStubState()
    vi.mocked(createWifiSetupState).mockReturnValue(stub as ReturnType<typeof makeStubState>)
    const { container } = render(WifiSetup, { props: { onSaved } })
    const input = container.querySelector('#wallet') as HTMLInputElement
    await fireEvent.input(input, { target: { value: 'bc1qexample' } })
    expect(stub.wallet).toBe('bc1qexample')
  })

  it('pool host input updates ws.poolHost', async () => {
    const stub = makeStubState()
    vi.mocked(createWifiSetupState).mockReturnValue(stub as ReturnType<typeof makeStubState>)
    const { container } = render(WifiSetup, { props: { onSaved } })
    const input = container.querySelector('#pool_host') as HTMLInputElement
    await fireEvent.input(input, { target: { value: 'pool.example.com' } })
    expect(stub.poolHost).toBe('pool.example.com')
  })

  it('pool port input updates ws.poolPort', async () => {
    const stub = makeStubState()
    vi.mocked(createWifiSetupState).mockReturnValue(stub as ReturnType<typeof makeStubState>)
    const { container } = render(WifiSetup, { props: { onSaved } })
    const input = container.querySelector('#pool_port') as HTMLInputElement
    await fireEvent.input(input, { target: { value: '4444' } })
    expect(stub.poolPort).toBe('4444')
  })

  it('pool pass input updates ws.poolPass', async () => {
    const stub = makeStubState()
    vi.mocked(createWifiSetupState).mockReturnValue(stub as ReturnType<typeof makeStubState>)
    const { container } = render(WifiSetup, { props: { onSaved } })
    const input = container.querySelector('#pool_pass') as HTMLInputElement
    await fireEvent.input(input, { target: { value: 'x' } })
    expect(stub.poolPass).toBe('x')
  })

  it('WifiSelect onselect updates ws.selectedSsid', async () => {
    const stub = makeStubState({
      networks: [{ ssid: 'TestNet', rssi: -50, secure: true }],
    })
    vi.mocked(createWifiSetupState).mockReturnValue(stub as ReturnType<typeof makeStubState>)
    const { container } = render(WifiSetup, { props: { onSaved } })
    // Open the WifiSelect dropdown trigger and pick the network
    const trigger = container.querySelector('.scan-controls button') as HTMLButtonElement
    await fireEvent.click(trigger)
    const option = container.querySelector('[role="option"]') as HTMLElement
    await fireEvent.click(option)
    expect(stub.selectedSsid).toBe('TestNet')
  })
})
