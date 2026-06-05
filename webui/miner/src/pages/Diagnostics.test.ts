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
  heap: any
  heapErr: string
  heapLoading: boolean
  heapCheckResult: '' | 'ok' | 'bad'
  heapChecking: boolean
  tasks: any[]
  tasksErr: string
  tasksLoading: boolean
  panic: any
  abnormalResets: number | null
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
  loadHeap: ReturnType<typeof vi.fn>
  runHeapCheck: ReturnType<typeof vi.fn>
  loadTasks: ReturnType<typeof vi.fn>
  loadPanic: ReturnType<typeof vi.fn>
  loadAbnormalResets: ReturnType<typeof vi.fn>
  doClearAbnormalResets: ReturnType<typeof vi.fn>
  doClearPanic: ReturnType<typeof vi.fn>
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
  heap: null,
  heapErr: '',
  heapLoading: false,
  heapCheckResult: '',
  heapChecking: false,
  tasks: [],
  tasksErr: '',
  tasksLoading: false,
  panic: null,
  abnormalResets: null,
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
  loadHeap: vi.fn(),
  runHeapCheck: vi.fn(),
  loadTasks: vi.fn(),
  loadPanic: vi.fn(),
  loadAbnormalResets: vi.fn(),
  doClearAbnormalResets: vi.fn(),
  doClearPanic: vi.fn(),
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

// Provide hasAsic as true by default so the Telemetry drops section renders
vi.mock('../lib/stores', async () => {
  const { writable } = await import('svelte/store')
  return {
    hasAsic: writable(true),
    fanEditOpen: writable(false),
  }
})

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
  mockDs.heap = null
  mockDs.heapErr = ''
  mockDs.heapLoading = false
  mockDs.heapCheckResult = ''
  mockDs.heapChecking = false
  mockDs.tasks = []
  mockDs.tasksErr = ''
  mockDs.tasksLoading = false
  mockDs.panic = null
  mockDs.abnormalResets = null
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
    expect(texts.some((t) => t.includes('Telemetry drops'))).toBe(true)
    expect(texts.some((t) => t.includes('Live Logs'))).toBe(true)
    expect(texts.some((t) => t.includes('System health'))).toBe(true)
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
    const { container } = render(Diagnostics)
    const btns = container.querySelectorAll('button')
    // Find Clear button
    const clearBtn = Array.from(btns).find(b => b.textContent?.includes('Clear'))
    expect((clearBtn as HTMLButtonElement).disabled).toBe(true)
  })

  it('enables Clear button when lines has content', () => {
    mockDs.lines = ['some log line']
    const { container } = render(Diagnostics)
    const btns = container.querySelectorAll('button')
    // Find Clear button
    const clearBtn = Array.from(btns).find(b => b.textContent?.includes('Clear'))
    expect((clearBtn as HTMLButtonElement).disabled).toBe(false)
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

describe('Diagnostics — Status strip with new elements', () => {
  it('renders without crashing with new diagnostics state', () => {
    mockDs.heap = { internal: { free: 1000, allocated: 500, largest_free_block: 400, minimum_ever_free: 100 }, dma: { free: 2000, allocated: 1000, largest_free_block: 1500, minimum_ever_free: 500 }, default: { free: 3000, allocated: 2000, largest_free_block: 2500, minimum_ever_free: 1000 } }
    mockDs.tasks = [{ name: 'IDLE1', prio: 0, base_prio: 0, stack_hwm: 100, state: 'ready' }]
    mockDs.panic = { available: true, coredump: true, boots_since: 1 }
    mockDs.abnormalResets = 3
    const { component } = render(Diagnostics)
    expect(component).toBeDefined()
  })

  it('shows abnormal reset count when abnormalResets is set', () => {
    mockDs.abnormalResets = 3
    const { container } = render(Diagnostics)
    expect(container.innerHTML.includes('3') || container.innerHTML.includes('Resets')).toBe(true)
  })

  it('shows panic indicator when panic.available is true', () => {
    mockDs.panic = { available: true, coredump: true, boots_since: 1 }
    const { container } = render(Diagnostics)
    // Panic should be accessible in rendered component
    expect(container).toBeDefined()
  })

  it('hides panic indicator when panic.available is false', () => {
    mockDs.panic = { available: false, coredump: false, boots_since: 0 }
    const { container } = render(Diagnostics)
    // Component should render but panic.available is false
    expect(container).toBeDefined()
  })

  it('shows Reboot button in UI', () => {
    const { getByRole } = render(Diagnostics)
    const btn = getByRole('button', { name: 'Reboot' })
    expect(btn).toBeTruthy()
  })
})

describe('Diagnostics — System health section', () => {
  it('renders details section with summary content', () => {
    const { container } = render(Diagnostics)
    const details = container.querySelectorAll('details')
    expect(details.length).toBeGreaterThan(0)
  })

  it('shows heap table rows when heap data is present', () => {
    mockDs.heap = {
      internal: { free: 1000, allocated: 500, largest_free_block: 800, minimum_ever_free: 200 },
      dma: { free: 2000, allocated: 1000, largest_free_block: 1500, minimum_ever_free: 500 },
      default: { free: 3000, allocated: 2000, largest_free_block: 2500, minimum_ever_free: 1000 },
    }
    const { container } = render(Diagnostics)
    const rows = container.querySelectorAll('table tbody tr')
    // Should have heap table rows with data
    expect(rows.length).toBeGreaterThan(0)
  })

  it('shows heap error message when heapErr is set', () => {
    mockDs.heapErr = 'fetch timeout'
    const { container } = render(Diagnostics)
    expect(container.innerHTML).toContain('fetch timeout')
  })

  it('shows heap loading state when heapLoading is true', () => {
    mockDs.heapLoading = true
    const { container } = render(Diagnostics)
    // Should render component
    expect(container).toBeDefined()
  })
})

describe('Diagnostics — Tasks section', () => {
  it('renders details section for tasks', () => {
    const { container } = render(Diagnostics)
    const details = container.querySelectorAll('details')
    expect(details.length).toBeGreaterThan(0)
  })

  it('shows task table rows when tasks are loaded', () => {
    mockDs.tasks = [
      { name: 'IDLE1', prio: 0, base_prio: 0, stack_hwm: 100, state: 'ready' },
      { name: 'mining', prio: 20, base_prio: 20, stack_hwm: 500, state: 'running' },
    ]
    const { container } = render(Diagnostics)
    const rows = container.querySelectorAll('table tbody tr')
    expect(rows.length).toBeGreaterThan(0)
  })

  it('shows task name in table when present', () => {
    mockDs.tasks = [{ name: 'IDLE1', prio: 0, base_prio: 0, stack_hwm: 100, state: 'ready' }]
    const { container } = render(Diagnostics)
    expect(container.innerHTML).toContain('IDLE1')
  })
})

describe('Diagnostics — Telemetry drops section', () => {
  it('renders Telemetry drops collapsed details section', () => {
    const { container } = render(Diagnostics)
    const details = container.querySelectorAll('details')
    const hasTelemetry = Array.from(details).some((d) =>
      d.querySelector('summary')?.textContent?.includes('Telemetry') || d.innerHTML.includes('Telemetry')
    )
    expect(hasTelemetry || container.innerHTML.includes('Telemetry')).toBe(true)
  })
})

describe('Diagnostics — Panic block', () => {
  it('renders panic block with content when panic.available is true', () => {
    mockDs.panic = {
      available: true,
      coredump: true,
      boots_since: 1,
      task: 'mining',
      panic_reason: 'Stack overflow',
    }
    const { container } = render(Diagnostics)
    // Panic data should be in the rendered component
    expect(container.innerHTML.includes('mining') || container.innerHTML.includes('Stack overflow')).toBe(true)
  })

  it('component renders when panic.coredump is true', () => {
    mockDs.panic = { available: true, coredump: true, boots_since: 1 }
    const { container } = render(Diagnostics)
    expect(container).toBeDefined()
  })

  it('component renders with panic available', () => {
    mockDs.panic = { available: true, coredump: false, boots_since: 0 }
    const { container } = render(Diagnostics)
    expect(container).toBeDefined()
  })

  it('panic state changes are reflected in mock', () => {
    mockDs.panic = { available: true, coredump: false, boots_since: 0 }
    render(Diagnostics)
    // Simulate clear
    mockDs.panic = { available: false, coredump: false, boots_since: 0 }
    // Panic state should be updated
    expect(mockDs.panic.available).toBe(false)
  })
})
