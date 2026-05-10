import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render } from '@testing-library/svelte'
import type { RecentDrop } from '../lib/api'
import type { SseStatus } from '../lib/sse'

// Mock the state machine — return a controllable stub
const mockDs: {
  recentDrops: RecentDrop[]
  status: SseStatus
  wasDisconnected: boolean
  nextRetryAt: number | null
  tickNow: number
  lines: string[]
  autoscroll: boolean
  filter: string
  panel: null
  retryInS: number | null
  filtered: string[]
  currentLevel: string | null
  availableLevels: string[]
  tagLevels: { tag: string; level: string }[]
  levelsLoading: boolean
  levelsErr: string
  selectedTag: string
  selectedLevel: string
  applying: boolean
  applyMsg: string
  applyKind: '' | 'ok' | 'err'
  rebooting: boolean
  rebootMsg: string
  showRebootDialog: boolean
  REBOOT_SKIP_KEY: string
  init: ReturnType<typeof vi.fn>
  destroy: ReturnType<typeof vi.fn>
  loadDiagAsic: ReturnType<typeof vi.fn>
  loadLevels: ReturnType<typeof vi.fn>
  applyLevel: ReturnType<typeof vi.fn>
  onLevelChange: ReturnType<typeof vi.fn>
  doReboot: ReturnType<typeof vi.fn>
  requestReboot: ReturnType<typeof vi.fn>
  cancelReboot: ReturnType<typeof vi.fn>
  clear: ReturnType<typeof vi.fn>
  onPanelScroll: ReturnType<typeof vi.fn>
  startStream: ReturnType<typeof vi.fn>
  onVisibilityChange: ReturnType<typeof vi.fn>
} = {
  recentDrops: [],
  status: 'connecting',
  wasDisconnected: false,
  nextRetryAt: null,
  tickNow: Date.now(),
  lines: [],
  autoscroll: true,
  filter: '',
  panel: null,
  retryInS: null,
  filtered: [],
  currentLevel: null,
  availableLevels: ['info', 'warn', 'error'],
  tagLevels: [],
  levelsLoading: false,
  levelsErr: '',
  selectedTag: '',
  selectedLevel: 'info',
  applying: false,
  applyMsg: '',
  applyKind: '',
  rebooting: false,
  rebootMsg: '',
  showRebootDialog: false,
  REBOOT_SKIP_KEY: 'taipanminer.skipRebootConfirm',
  init: vi.fn(),
  destroy: vi.fn(),
  loadDiagAsic: vi.fn(),
  loadLevels: vi.fn(),
  applyLevel: vi.fn(),
  onLevelChange: vi.fn(),
  doReboot: vi.fn(),
  requestReboot: vi.fn(),
  cancelReboot: vi.fn(),
  clear: vi.fn(),
  onPanelScroll: vi.fn(),
  startStream: vi.fn(),
  onVisibilityChange: vi.fn(),
}

vi.mock('../lib/diagnosticsState.svelte', () => ({
  createDiagnosticsState: () => mockDs,
}))

// Still mock api to avoid real fetch in case anything leaks
vi.mock('../lib/api', () => ({
  fetchStats: vi.fn(), fetchInfo: vi.fn(), fetchPower: vi.fn(), fetchFan: vi.fn(),
  fetchSettings: vi.fn(), fetchPool: vi.fn(), fetchHealth: vi.fn(), ping: vi.fn(),
  postReboot: vi.fn(), setLogLevel: vi.fn(), fetchLogLevels: vi.fn(), fetchDiagAsic: vi.fn(),
}))

import Diagnostics from './Diagnostics.svelte'

beforeEach(() => {
  vi.clearAllMocks()
  // Reset to clean defaults
  mockDs.recentDrops = []
  mockDs.status = 'connecting'
  mockDs.lines = []
  mockDs.filtered = []
  mockDs.filter = ''
  mockDs.levelsErr = ''
  mockDs.applyMsg = ''
  mockDs.applyKind = ''
  mockDs.rebooting = false
  mockDs.rebootMsg = ''
  mockDs.tagLevels = []
  mockDs.levelsLoading = false
})

describe('Diagnostics — UI rendering', () => {
  it('renders without crashing', () => {
    const { component } = render(Diagnostics)
    expect(component).toBeDefined()
  })

  it('shows "No recent drops." when recentDrops is empty', () => {
    mockDs.recentDrops = []
    const { getByText } = render(Diagnostics)
    expect(getByText('No recent drops.')).toBeTruthy()
  })

  it('renders drop table rows when recentDrops has entries', () => {
    mockDs.recentDrops = [
      { ts_ago_s: 30, chip: 0, kind: 'total', domain: 0, addr: 0, ghs: 100, delta: -20, elapsed_s: 1 },
    ]
    const { container } = render(Diagnostics)
    const rows = container.querySelectorAll('table.drops tbody tr')
    expect(rows).toHaveLength(1)
  })

  it('renders headings', () => {
    const { container } = render(Diagnostics)
    const headings = container.querySelectorAll('h2')
    const texts = Array.from(headings).map((h) => h.textContent ?? '')
    expect(texts.some((t) => t.includes('Recent telemetry drops'))).toBe(true)
    expect(texts.some((t) => t.includes('Device'))).toBe(true)
    expect(texts.some((t) => t.includes('Live Logs'))).toBe(true)
  })

  it('shows Connecting status when status=connecting', () => {
    mockDs.status = 'connecting'
    const { getByText } = render(Diagnostics)
    expect(getByText('Connecting…')).toBeTruthy()
  })

  it('shows Connected status when status=connected', () => {
    mockDs.status = 'connected'
    const { getByText } = render(Diagnostics)
    expect(getByText('Connected')).toBeTruthy()
  })

  it('shows External client connected when status=external', () => {
    mockDs.status = 'external'
    const { getByText } = render(Diagnostics)
    expect(getByText('External client connected')).toBeTruthy()
  })

  it('shows Reboot button (not rebooting)', () => {
    mockDs.rebooting = false
    const { getByRole } = render(Diagnostics)
    const btn = getByRole('button', { name: 'Reboot' })
    expect(btn).toBeTruthy()
    expect((btn as HTMLButtonElement).disabled).toBe(false)
  })

  it('disables Reboot button when rebooting=true', () => {
    mockDs.rebooting = true
    const { getByRole } = render(Diagnostics)
    // Button text changes to "Rebooting…" when rebooting
    const btn = getByRole('button', { name: 'Rebooting…' })
    expect((btn as HTMLButtonElement).disabled).toBe(true)
  })

  it('shows levelsErr message when set', () => {
    mockDs.levelsErr = 'fetch failed'
    const { getByText } = render(Diagnostics)
    expect(getByText('fetch failed')).toBeTruthy()
  })

  it('shows applyMsg when set', () => {
    mockDs.applyMsg = 'wifi → warn'
    mockDs.applyKind = 'ok'
    const { getByText } = render(Diagnostics)
    expect(getByText('wifi → warn')).toBeTruthy()
  })

  it('shows rebootMsg when set', () => {
    mockDs.rebootMsg = 'Reboot failed: timeout'
    const { getByText } = render(Diagnostics)
    expect(getByText('Reboot failed: timeout')).toBeTruthy()
  })

  it('shows filter hint when filter is set', () => {
    mockDs.filter = 'error'
    mockDs.filtered = ['error line']
    mockDs.lines = ['error line', 'info line']
    const { getByText } = render(Diagnostics)
    expect(getByText('1 of 2 lines match')).toBeTruthy()
  })

  it('does not show filter hint when filter is empty', () => {
    mockDs.filter = ''
    const { container } = render(Diagnostics)
    expect(container.querySelector('.filter-hint')).toBeNull()
  })

  it('disables Clear button when lines is empty', () => {
    mockDs.lines = []
    const { getByRole } = render(Diagnostics)
    const btn = getByRole('button', { name: 'Clear' })
    expect((btn as HTMLButtonElement).disabled).toBe(true)
  })

  it('enables Clear button when lines has content', () => {
    mockDs.lines = ['some log line']
    const { getByRole } = render(Diagnostics)
    const btn = getByRole('button', { name: 'Clear' })
    expect((btn as HTMLButtonElement).disabled).toBe(false)
  })

  it('renders log level selects disabled when no tags loaded', () => {
    mockDs.tagLevels = []
    mockDs.levelsLoading = false
    mockDs.applying = false
    const { container } = render(Diagnostics)
    const selects = container.querySelectorAll('select')
    // tag select (first) should be disabled when tagLevels.length === 0
    expect((selects[0] as HTMLSelectElement).disabled).toBe(true)
  })

  it('calls init on mount', () => {
    render(Diagnostics)
    expect(mockDs.init).toHaveBeenCalled()
  })
})
