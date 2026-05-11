import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render } from '@testing-library/svelte'

// Provide minimal state shape
function makeState(overrides: Record<string, unknown> = {}) {
  return {
    board: '',
    connectStatus: 'idle',
    connectError: null,
    chipInfo: null,
    flashStatus: 'idle',
    flashError: null,
    manifest: null,
    manifestError: null,
    downloadProgress: null,
    flashProgress: null,
    deviceDisconnected: false,
    boardOptions: [],
    transport: null,
    loadManifestAction: vi.fn().mockResolvedValue(undefined),
    selectBoard: vi.fn(),
    connect: vi.fn(),
    disconnect: vi.fn(),
    flash: vi.fn(),
    flashAnother: vi.fn(),
    handleDeviceDisconnect: vi.fn(),
    ...overrides,
  }
}

const createFlashStateMock = vi.hoisted(() => vi.fn(() => makeState()))

vi.mock('./lib/flashState.svelte', () => ({
  createFlashState: createFlashStateMock,
}))

beforeEach(() => {
  createFlashStateMock.mockImplementation(() => makeState())
  vi.clearAllMocks()
})

afterEach(() => {
  vi.restoreAllMocks()
})

async function renderApp(stateOverrides: Record<string, unknown> = {}) {
  createFlashStateMock.mockImplementation(() => makeState(stateOverrides))
  const { default: App } = await import('./App.svelte')
  return render(App)
}

describe('App.svelte', () => {
  it('renders without crashing in idle state', async () => {
    const { container } = await renderApp()
    expect(container).toBeInTheDocument()
  })

  it('renders Connect button in idle state', async () => {
    const { getByText } = await renderApp()
    expect(getByText('Connect device')).toBeInTheDocument()
  })

  it('Select onchange routes through state.selectBoard', async () => {
    const selectBoard = vi.fn()
    const { container } = await renderApp({
      selectBoard,
      boardOptions: [
        { value: 'tdongle-s3', label: 'tdongle-s3' },
        { value: 'bitaxe-601', label: 'bitaxe-601' },
      ],
    })
    const select = container.querySelector('select') as HTMLSelectElement
    const { fireEvent } = await import('@testing-library/svelte')
    await fireEvent.change(select, { target: { value: 'bitaxe-601' } })
    expect(selectBoard).toHaveBeenCalledWith('bitaxe-601')
  })

  it('shows Connecting… when connectStatus=connecting', async () => {
    const { getByText } = await renderApp({ connectStatus: 'connecting' })
    expect(getByText('Connecting…')).toBeInTheDocument()
  })

  it('shows ✓ Connected and Disconnect button when connected', async () => {
    const { getByText } = await renderApp({
      connectStatus: 'connected',
      chipInfo: { chip: 'ESP32-S3', mac: 'AA:BB:CC:DD:EE:FF', flashSize: '4MB' },
      manifest: { tag: 'v1.0.0', publishedAt: '', assets: {} },
    })
    expect(getByText('✓ Connected')).toBeInTheDocument()
    expect(getByText('Disconnect')).toBeInTheDocument()
  })

  it('shows chip info when connected', async () => {
    const { getByText } = await renderApp({
      connectStatus: 'connected',
      chipInfo: { chip: 'ESP32-S3', mac: 'AA:BB:CC:DD:EE:FF', flashSize: '4MB' },
    })
    expect(getByText('ESP32-S3')).toBeInTheDocument()
    expect(getByText('AA:BB:CC:DD:EE:FF')).toBeInTheDocument()
    expect(getByText('4MB')).toBeInTheDocument()
  })

  it('shows progress bar during downloading', async () => {
    const { container } = await renderApp({
      flashStatus: 'downloading',
      downloadProgress: { loaded: 50, total: 100 },
    })
    expect(container.querySelector('.progress')).toBeInTheDocument()
    expect(container.querySelector('.progress-msg')).toBeInTheDocument()
  })

  it('shows flashing progress bar during flashing', async () => {
    const { container } = await renderApp({
      flashStatus: 'flashing',
      flashProgress: { written: 10, total: 100 },
    })
    expect(container.querySelector('.progress')).toBeInTheDocument()
    const msg = container.querySelector('.progress-msg')
    expect(msg?.textContent).toContain('Flashing')
  })

  it('shows Flash another device button and success message when done', async () => {
    const { getByText } = await renderApp({ flashStatus: 'done' })
    expect(getByText('Flash another device')).toBeInTheDocument()
    expect(getByText(/Flash complete/)).toBeInTheDocument()
  })

  it('shows flash error message on error', async () => {
    const { getByText } = await renderApp({
      flashStatus: 'error',
      flashError: 'Device disconnected mid-flash — the firmware on the device is now in an unknown state. Plug it back in, click Connect, and re-flash.',
    })
    expect(getByText(/Device disconnected mid-flash/)).toBeInTheDocument()
  })

  it('shows device disconnected banner when deviceDisconnected=true', async () => {
    const { getByText } = await renderApp({ deviceDisconnected: true })
    expect(getByText(/Device disconnected\. Plug it back in/)).toBeInTheDocument()
  })

  it('shows connect error', async () => {
    const { getByText } = await renderApp({
      connectStatus: 'error',
      connectError: 'Device disconnected — plug it back in and click Connect to retry',
    })
    expect(getByText(/Device disconnected — plug it back in/)).toBeInTheDocument()
  })
})
