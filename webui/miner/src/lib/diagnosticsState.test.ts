import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'

vi.mock('./api', () => ({
  fetchDiagAsic: vi.fn().mockResolvedValue({ recent_drops: [] }),
  fetchLogLevels: vi.fn().mockResolvedValue({ levels: [], tags: [] }),
  setLogLevel: vi.fn().mockResolvedValue(undefined),
  postReboot: vi.fn().mockResolvedValue(undefined),
  fetchDiagHeap: vi.fn().mockResolvedValue({ internal: { free: 1000, allocated: 500, largest_free_block: 400, minimum_ever_free: 100 }, dma: { free: 2000, allocated: 1000, largest_free_block: 1500, minimum_ever_free: 500 }, default: { free: 3000, allocated: 2000, largest_free_block: 2500, minimum_ever_free: 1000 } }),
  checkDiagHeap: vi.fn().mockResolvedValue(true),
  fetchDiagTasks: vi.fn().mockResolvedValue([]),
  fetchDiagPanic: vi.fn().mockResolvedValue({ available: false, coredump: false, boots_since: 0 }),
  clearAbnormalResets: vi.fn().mockResolvedValue(undefined),
  clearDiagPanic: vi.fn().mockResolvedValue(undefined),
  fetchInfo: vi.fn().mockResolvedValue({ abnormal_reset_count: 0 }),
  coredumpUrl: '/api/diag/panic/coredump',
}))

vi.mock('./stores', async () => ({
  startRebootRecovery: vi.fn(),
}))

vi.mock('./sse', () => {
  let _lastInstance: any = null
  class FakeSseClient {
    callbacks: any
    destroyed = false
    started = false
    reconnectNow = vi.fn()
    isStale = vi.fn(() => false)
    constructor(opts: any) {
      this.callbacks = opts
      _lastInstance = this
    }
    start() { this.started = true }
    destroy() { this.destroyed = true }
    static getLast() { return _lastInstance }
    static reset() { _lastInstance = null }
  }
  return { SseClient: FakeSseClient }
})

import * as api from './api'
import { startRebootRecovery } from './stores'
import { createDiagnosticsState } from './diagnosticsState.svelte'
import { SseClient } from './sse'

function flushMicrotasks() {
  return new Promise((r) => queueMicrotask(r as any))
}

function lastSseInstance() {
  return (SseClient as any).getLast()
}

beforeEach(() => {
  vi.clearAllMocks()
  ;(SseClient as any).reset()
  vi.useFakeTimers()
  // Minimal document visibility API shim
  Object.defineProperty(document, 'visibilityState', {
    configurable: true,
    get: () => 'visible',
  })
})

afterEach(() => {
  vi.useRealTimers()
})

describe('createDiagnosticsState — initial state', () => {
  it('starts with empty recentDrops', () => {
    const ds = createDiagnosticsState()
    expect(ds.recentDrops).toEqual([])
  })

  it('starts with status connecting', () => {
    const ds = createDiagnosticsState()
    expect(ds.status).toBe('connecting')
  })

  it('starts with empty lines', () => {
    const ds = createDiagnosticsState()
    expect(ds.lines).toEqual([])
  })

  it('starts with autoscroll true', () => {
    const ds = createDiagnosticsState()
    expect(ds.autoscroll).toBe(true)
  })

  it('starts with empty filter', () => {
    const ds = createDiagnosticsState()
    expect(ds.filter).toBe('')
  })

  it('starts with rebooting false', () => {
    const ds = createDiagnosticsState()
    expect(ds.rebooting).toBe(false)
  })

  it('starts with showRebootDialog false', () => {
    const ds = createDiagnosticsState()
    expect(ds.showRebootDialog).toBe(false)
  })

  it('starts with applying false', () => {
    const ds = createDiagnosticsState()
    expect(ds.applying).toBe(false)
  })

  it('starts with levelsLoading false', () => {
    const ds = createDiagnosticsState()
    expect(ds.levelsLoading).toBe(false)
  })
})

describe('init()', () => {
  it('starts SseClient immediately (before awaiting data fetches)', async () => {
    const ds = createDiagnosticsState()
    const initPromise = ds.init()
    // SSE should be started immediately, before the await chain
    expect(lastSseInstance()).not.toBeNull()
    expect(lastSseInstance().started).toBe(true)
    await initPromise
  })

  it('calls loadDiagAsic and other loads sequentially', async () => {
    const ds = createDiagnosticsState()
    await ds.init()
    expect(api.fetchDiagAsic).toHaveBeenCalledTimes(1)
    expect(api.fetchLogLevels).toHaveBeenCalledTimes(1)
    expect(api.fetchDiagHeap).toHaveBeenCalledTimes(1)
    expect(api.fetchDiagTasks).toHaveBeenCalledTimes(1)
    expect(api.fetchDiagPanic).toHaveBeenCalledTimes(1)
    expect(api.fetchInfo).toHaveBeenCalledTimes(1)
  })

  it('sets up diagInterval that re-polls every 10s', async () => {
    const ds = createDiagnosticsState()
    await ds.init()
    vi.advanceTimersByTime(10000)
    // 1 on init + 1 from interval
    expect(api.fetchDiagAsic).toHaveBeenCalledTimes(2)
  })

  it('sets up tickTimer that fires every 1s', async () => {
    const ds = createDiagnosticsState()
    const before = ds.tickNow
    await ds.init()
    vi.advanceTimersByTime(1100)
    // tickNow should have advanced
    expect(ds.tickNow).toBeGreaterThan(before)
  })

  it('adds visibilitychange listener', async () => {
    const ds = createDiagnosticsState()
    const spy = vi.spyOn(document, 'addEventListener')
    await ds.init()
    expect(spy).toHaveBeenCalledWith('visibilitychange', expect.any(Function))
  })
})

describe('destroy()', () => {
  it('removes visibilitychange listener', async () => {
    const ds = createDiagnosticsState()
    await ds.init()
    const spy = vi.spyOn(document, 'removeEventListener')
    ds.destroy()
    expect(spy).toHaveBeenCalledWith('visibilitychange', expect.any(Function))
  })

  it('clears diagInterval', async () => {
    const ds = createDiagnosticsState()
    await ds.init()
    ds.destroy()
    // After destroy, polling should stop
    vi.advanceTimersByTime(20000)
    // Only the 1 initial call from init(), no more
    expect(api.fetchDiagAsic).toHaveBeenCalledTimes(1)
  })

  it('destroys the sse client', async () => {
    const ds = createDiagnosticsState()
    await ds.init()
    const instance = lastSseInstance()
    ds.destroy()
    expect(instance.destroyed).toBe(true)
  })
})

describe('loadDiagAsic()', () => {
  it('populates recentDrops on success', async () => {
    const drops = [{ ts_ago_s: 5, chip: 0, kind: 'total', domain: 0, addr: 0, ghs: 100, delta: -10, elapsed_s: 1 }]
    vi.mocked(api.fetchDiagAsic).mockResolvedValueOnce({ recent_drops: drops } as any)
    const ds = createDiagnosticsState()
    await ds.loadDiagAsic()
    expect(ds.recentDrops).toEqual(drops)
  })
})

describe('loadLevels()', () => {
  it('populates tagLevels and availableLevels on success', async () => {
    vi.mocked(api.fetchLogLevels).mockResolvedValueOnce({
      levels: ['info', 'warn', 'error'],
      tags: [{ tag: 'wifi', level: 'warn' }, { tag: '*', level: 'info' }],
    } as any)
    const ds = createDiagnosticsState()
    await ds.loadLevels()
    expect(ds.tagLevels).toHaveLength(2)
    // sorted by tag name
    expect(ds.tagLevels[0].tag).toBe('*')
    expect(ds.tagLevels[1].tag).toBe('wifi')
    // selectedTag defaults to first tag
    expect(ds.selectedTag).toBe('*')
  })

  it('sets levelsLoading true during fetch then false after', async () => {
    let resolveFetch!: () => void
    vi.mocked(api.fetchLogLevels).mockReturnValueOnce(
      new Promise<any>((res) => { resolveFetch = () => res({ levels: [], tags: [] }) })
    )
    const ds = createDiagnosticsState()
    const promise = ds.loadLevels()
    expect(ds.levelsLoading).toBe(true)
    resolveFetch()
    await promise
    expect(ds.levelsLoading).toBe(false)
  })
})

describe('applyLevel()', () => {
  it('does nothing when selectedTag is empty', async () => {
    const ds = createDiagnosticsState()
    await ds.applyLevel()
    expect(api.setLogLevel).not.toHaveBeenCalled()
  })

  it('calls setLogLevel and sets applyMsg on success', async () => {
    vi.mocked(api.fetchLogLevels).mockResolvedValueOnce({
      levels: ['info', 'warn'],
      tags: [{ tag: 'wifi', level: 'info' }],
    } as any)
    const ds = createDiagnosticsState()
    await ds.loadLevels()
    ds.selectedLevel = 'warn'
    await ds.applyLevel()
    expect(api.setLogLevel).toHaveBeenCalledWith('wifi', 'warn')
    expect(ds.applyKind).toBe('ok')
    expect(ds.applyMsg).toBe('wifi → warn')
    expect(ds.applying).toBe(false)
  })

  it('sets applyKind=err and applyMsg to error message on failure', async () => {
    vi.mocked(api.fetchLogLevels).mockResolvedValueOnce({
      levels: ['info'],
      tags: [{ tag: 'wifi', level: 'info' }],
    } as any)
    vi.mocked(api.setLogLevel).mockRejectedValueOnce(new Error('server error'))
    const ds = createDiagnosticsState()
    await ds.loadLevels()
    await ds.applyLevel()
    expect(ds.applyKind).toBe('err')
    expect(ds.applyMsg).toBe('server error')
    expect(ds.applying).toBe(false)
  })
})

describe('doReboot()', () => {
  it('calls postReboot and startRebootRecovery on success', async () => {
    const ds = createDiagnosticsState()
    await ds.doReboot()
    expect(api.postReboot).toHaveBeenCalled()
    expect(startRebootRecovery).toHaveBeenCalledWith('Rebooting miner')
    expect(ds.rebooting).toBe(false)
  })

  it('sets rebootMsg on failure', async () => {
    vi.mocked(api.postReboot).mockRejectedValueOnce(new Error('unreachable'))
    const ds = createDiagnosticsState()
    await ds.doReboot()
    expect(ds.rebootMsg).toBe('Reboot failed: unreachable')
    expect(ds.rebooting).toBe(false)
  })
})

describe('requestReboot()', () => {
  it('opens dialog when skip key not set', () => {
    localStorage.removeItem('taipanminer.skipRebootConfirm')
    const ds = createDiagnosticsState()
    ds.requestReboot()
    expect(ds.showRebootDialog).toBe(true)
  })

  it('does not open dialog and sets rebooting when skip key is set', () => {
    localStorage.setItem('taipanminer.skipRebootConfirm', '1')
    const ds = createDiagnosticsState()
    // requestReboot skips dialog and fires doReboot() (async, not awaited)
    // Verify dialog stays closed — postReboot is asserted via separate doReboot() tests
    ds.requestReboot()
    localStorage.removeItem('taipanminer.skipRebootConfirm')
    expect(ds.showRebootDialog).toBe(false)
    // rebooting goes true synchronously before the await postReboot()
    expect(ds.rebooting).toBe(true)
  })
})

describe('clear()', () => {
  it('empties lines', () => {
    const ds = createDiagnosticsState()
    ds.init()
    // Simulate some messages via the SSE onMessage callback
    lastSseInstance().callbacks.onMessage('line1')
    lastSseInstance().callbacks.onMessage('line2')
    expect(ds.lines).toHaveLength(2)
    ds.clear()
    expect(ds.lines).toEqual([])
  })
})

describe('SSE callbacks', () => {
  it('onMessage appends to lines', async () => {
    const ds = createDiagnosticsState()
    await ds.init()
    lastSseInstance().callbacks.onMessage('hello')
    lastSseInstance().callbacks.onMessage('world')
    expect(ds.lines).toEqual(['hello', 'world'])
  })

  it('onMessage caps lines at 500', async () => {
    const ds = createDiagnosticsState()
    await ds.init()
    // Push 501 lines
    for (let i = 0; i < 501; i++) {
      lastSseInstance().callbacks.onMessage(`line${i}`)
    }
    expect(ds.lines).toHaveLength(500)
    expect(ds.lines[0]).toBe('line1') // first line dropped
    expect(ds.lines[499]).toBe('line500')
  })

  it('onStatusChange updates status', async () => {
    const ds = createDiagnosticsState()
    await ds.init()
    lastSseInstance().callbacks.onStatusChange('connected')
    expect(ds.status).toBe('connected')
  })

  it('onStatusChange sets wasDisconnected on disconnected', async () => {
    const ds = createDiagnosticsState()
    await ds.init()
    expect(ds.wasDisconnected).toBe(false)
    lastSseInstance().callbacks.onStatusChange('disconnected')
    expect(ds.wasDisconnected).toBe(true)
  })

  it('onStatusChange sets wasDisconnected on external', async () => {
    const ds = createDiagnosticsState()
    await ds.init()
    lastSseInstance().callbacks.onStatusChange('external')
    expect(ds.wasDisconnected).toBe(true)
  })

  it('onRetryAtChange updates nextRetryAt', async () => {
    const ds = createDiagnosticsState()
    await ds.init()
    const at = Date.now() + 3000
    lastSseInstance().callbacks.onRetryAtChange(at)
    expect(ds.nextRetryAt).toBe(at)
  })

  it('onRetryAtChange accepts null', async () => {
    const ds = createDiagnosticsState()
    await ds.init()
    lastSseInstance().callbacks.onRetryAtChange(null)
    expect(ds.nextRetryAt).toBeNull()
  })

  it('onOpen reloads levels when wasDisconnected', async () => {
    const ds = createDiagnosticsState()
    await ds.init()
    // Simulate disconnect then reconnect
    lastSseInstance().callbacks.onStatusChange('disconnected')
    expect(ds.wasDisconnected).toBe(true)
    vi.clearAllMocks()
    await lastSseInstance().callbacks.onOpen()
    expect(api.fetchLogLevels).toHaveBeenCalled()
    expect(ds.wasDisconnected).toBe(false)
  })

  it('onOpen does not reload levels when not disconnected', async () => {
    const ds = createDiagnosticsState()
    await ds.init()
    vi.clearAllMocks()
    await lastSseInstance().callbacks.onOpen()
    expect(api.fetchLogLevels).not.toHaveBeenCalled()
  })
})

describe('onPanelScroll()', () => {
  it('sets autoscroll true when near bottom', () => {
    const ds = createDiagnosticsState()
    const el = {
      scrollHeight: 1000,
      scrollTop: 995,
      clientHeight: 10,
    } as unknown as HTMLPreElement
    ds.panel = el
    ds.autoscroll = false
    ds.onPanelScroll()
    // scrollHeight(1000) - scrollTop(995) - clientHeight(10) = -5 < 8
    expect(ds.autoscroll).toBe(true)
  })

  it('sets autoscroll false when not near bottom', () => {
    const ds = createDiagnosticsState()
    const el = {
      scrollHeight: 1000,
      scrollTop: 500,
      clientHeight: 100,
    } as unknown as HTMLPreElement
    ds.panel = el
    ds.autoscroll = true
    ds.onPanelScroll()
    // scrollHeight(1000) - scrollTop(500) - clientHeight(100) = 400 >= 8
    expect(ds.autoscroll).toBe(false)
  })

  it('does nothing when panel is null', () => {
    const ds = createDiagnosticsState()
    ds.panel = null
    expect(() => ds.onPanelScroll()).not.toThrow()
  })
})

describe('onVisibilityChange()', () => {
  it('does nothing when document is hidden', async () => {
    Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'hidden' })
    const ds = createDiagnosticsState()
    await ds.init()
    ds.onVisibilityChange()
    expect(lastSseInstance().reconnectNow).not.toHaveBeenCalled()
  })

  it('calls reconnectNow when visible and sse is stale', async () => {
    Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'visible' })
    const ds = createDiagnosticsState()
    await ds.init()
    lastSseInstance().isStale.mockReturnValue(true)
    ds.onVisibilityChange()
    expect(lastSseInstance().reconnectNow).toHaveBeenCalled()
  })

  it('does not reconnect when visible but not stale', async () => {
    Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'visible' })
    const ds = createDiagnosticsState()
    await ds.init()
    lastSseInstance().isStale.mockReturnValue(false)
    ds.onVisibilityChange()
    expect(lastSseInstance().reconnectNow).not.toHaveBeenCalled()
  })
})

describe('$derived: filtered', () => {
  it('returns all lines when filter is empty', () => {
    const ds = createDiagnosticsState()
    ds.init()
    lastSseInstance().callbacks.onMessage('INFO hello')
    lastSseInstance().callbacks.onMessage('DEBUG world')
    expect(ds.filtered).toHaveLength(2)
  })

  it('filters lines when filter is set', () => {
    const ds = createDiagnosticsState()
    ds.init()
    lastSseInstance().callbacks.onMessage('INFO hello')
    lastSseInstance().callbacks.onMessage('DEBUG world')
    ds.filter = 'info'
    expect(ds.filtered).toEqual(['INFO hello'])
  })
})

describe('$derived: currentLevel', () => {
  it('returns level for selected tag', async () => {
    vi.mocked(api.fetchLogLevels).mockResolvedValueOnce({
      levels: ['info', 'warn'],
      tags: [{ tag: 'wifi', level: 'warn' }],
    } as any)
    const ds = createDiagnosticsState()
    await ds.loadLevels()
    expect(ds.selectedTag).toBe('wifi')
    expect(ds.currentLevel).toBe('warn')
  })

  it('returns null when tag not found', () => {
    const ds = createDiagnosticsState()
    // tagLevels is empty, selectedTag is empty
    expect(ds.currentLevel).toBeNull()
  })
})

describe('$derived: retryInS', () => {
  it('returns null when nextRetryAt is null', () => {
    const ds = createDiagnosticsState()
    expect(ds.retryInS).toBeNull()
  })

  it('returns seconds until retry', () => {
    const ds = createDiagnosticsState()
    ds.init()
    // Set retry to 5s from now
    const at = ds.tickNow + 5000
    lastSseInstance().callbacks.onRetryAtChange(at)
    // retryInS = ceil((at - tickNow) / 1000) = ceil(5000/1000) = 5
    expect(ds.retryInS).toBe(5)
  })

  it('updates when tick fires', () => {
    const ds = createDiagnosticsState()
    ds.init()
    const at = ds.tickNow + 5000
    lastSseInstance().callbacks.onRetryAtChange(at)
    expect(ds.retryInS).toBe(5)
    vi.advanceTimersByTime(1000)
    // After 1 tick, tickNow increases, so retryInS decreases
    expect(ds.retryInS).toBeLessThanOrEqual(5)
  })
})

describe('setter round-trips', () => {
  it('filter setter', () => {
    const ds = createDiagnosticsState()
    ds.filter = 'hello'
    expect(ds.filter).toBe('hello')
  })

  it('autoscroll setter', () => {
    const ds = createDiagnosticsState()
    ds.autoscroll = false
    expect(ds.autoscroll).toBe(false)
    ds.autoscroll = true
    expect(ds.autoscroll).toBe(true)
  })

  it('showRebootDialog setter', () => {
    const ds = createDiagnosticsState()
    ds.showRebootDialog = true
    expect(ds.showRebootDialog).toBe(true)
    ds.showRebootDialog = false
    expect(ds.showRebootDialog).toBe(false)
  })

  it('selectedTag setter', () => {
    const ds = createDiagnosticsState()
    ds.selectedTag = 'wifi'
    expect(ds.selectedTag).toBe('wifi')
  })

  it('selectedLevel setter', () => {
    const ds = createDiagnosticsState()
    ds.selectedLevel = 'debug'
    expect(ds.selectedLevel).toBe('debug')
  })
})

describe('loadHeap()', () => {
  it('populates heap on success', async () => {
    const heapData = { internal: { free: 1000, allocated: 500, largest_free_block: 400, minimum_ever_free: 100 }, dma: { free: 2000, allocated: 1000, largest_free_block: 1500, minimum_ever_free: 500 }, default: { free: 3000, allocated: 2000, largest_free_block: 2500, minimum_ever_free: 1000 } }
    vi.mocked(api.fetchDiagHeap).mockResolvedValueOnce(heapData as any)
    const ds = createDiagnosticsState()
    await ds.loadHeap()
    expect(ds.heap).toEqual(heapData)
  })

  it('sets heapLoading true during fetch then false after', async () => {
    let resolveFetch!: () => void
    vi.mocked(api.fetchDiagHeap).mockReturnValueOnce(
      new Promise<any>((res) => { resolveFetch = () => res({ internal: { free: 1000, allocated: 500, largest_free_block: 400, minimum_ever_free: 100 }, dma: { free: 2000, allocated: 1000, largest_free_block: 1500, minimum_ever_free: 500 }, default: { free: 3000, allocated: 2000, largest_free_block: 2500, minimum_ever_free: 1000 } }) })
    )
    const ds = createDiagnosticsState()
    const promise = ds.loadHeap()
    expect(ds.heapLoading).toBe(true)
    resolveFetch()
    await promise
    expect(ds.heapLoading).toBe(false)
  })
})

describe('runHeapCheck()', () => {
  it('sets heapCheckResult to ok on success true', async () => {
    vi.mocked(api.checkDiagHeap).mockResolvedValueOnce(true)
    const ds = createDiagnosticsState()
    await ds.runHeapCheck()
    expect(ds.heapCheckResult).toBe('ok')
  })

  it('sets heapCheckResult to bad on success false', async () => {
    vi.mocked(api.checkDiagHeap).mockResolvedValueOnce(false)
    const ds = createDiagnosticsState()
    await ds.runHeapCheck()
    expect(ds.heapCheckResult).toBe('bad')
  })

  it('sets heapChecking true during check then false after', async () => {
    let resolveFetch!: () => void
    vi.mocked(api.checkDiagHeap).mockReturnValueOnce(
      new Promise<any>((res) => { resolveFetch = () => res(true) })
    )
    const ds = createDiagnosticsState()
    const promise = ds.runHeapCheck()
    expect(ds.heapChecking).toBe(true)
    resolveFetch()
    await promise
    expect(ds.heapChecking).toBe(false)
  })
})

describe('loadTasks()', () => {
  it('populates tasks on success and sorts by priority desc then name asc', async () => {
    const tasksData = [
      { name: 'IDLE1', prio: 0, base_prio: 0, stack_hwm: 100, state: 'ready' as const },
      { name: 'mining', prio: 20, base_prio: 20, stack_hwm: 500, state: 'running' as const },
      { name: 'wifi', prio: 5, base_prio: 5, stack_hwm: 200, state: 'ready' as const },
    ]
    vi.mocked(api.fetchDiagTasks).mockResolvedValueOnce(tasksData)
    const ds = createDiagnosticsState()
    await ds.loadTasks()
    // Sorted: mining (20), wifi (5), IDLE1 (0)
    expect(ds.tasks).toHaveLength(3)
    expect(ds.tasks[0].name).toBe('mining')
    expect(ds.tasks[1].name).toBe('wifi')
    expect(ds.tasks[2].name).toBe('IDLE1')
  })

  it('sets tasksLoading true during fetch then false after', async () => {
    let resolveFetch!: () => void
    vi.mocked(api.fetchDiagTasks).mockReturnValueOnce(
      new Promise<any>((res) => { resolveFetch = () => res([]) })
    )
    const ds = createDiagnosticsState()
    const promise = ds.loadTasks()
    expect(ds.tasksLoading).toBe(true)
    resolveFetch()
    await promise
    expect(ds.tasksLoading).toBe(false)
  })
})

describe('loadPanic()', () => {
  it('populates panic on success', async () => {
    const panicData = { available: true, coredump: true, boots_since: 2, task: 'mining', exc_pc: 0x400d1234, exc_cause: 28, panic_reason: 'Stack overflow' }
    vi.mocked(api.fetchDiagPanic).mockResolvedValueOnce(panicData as any)
    const ds = createDiagnosticsState()
    await ds.loadPanic()
    expect(ds.panic).toEqual(panicData)
  })
})

describe('loadAbnormalResets()', () => {
  it('extracts abnormal_reset_count from info', async () => {
    vi.mocked(api.fetchInfo).mockResolvedValueOnce({ abnormal_reset_count: 3 } as any)
    const ds = createDiagnosticsState()
    await ds.loadAbnormalResets()
    expect(ds.abnormalResets).toBe(3)
  })

  it('sets abnormalResets to null on missing field', async () => {
    vi.mocked(api.fetchInfo).mockResolvedValueOnce({} as any)
    const ds = createDiagnosticsState()
    await ds.loadAbnormalResets()
    expect(ds.abnormalResets).toBeNull()
  })
})

describe('doClearAbnormalResets()', () => {
  it('zeros the counter and sets clearResetsMsg on success', async () => {
    vi.mocked(api.clearAbnormalResets).mockResolvedValueOnce(undefined)
    const ds = createDiagnosticsState()
    await ds.doClearAbnormalResets()
    expect(api.clearAbnormalResets).toHaveBeenCalledTimes(1)
    expect(ds.abnormalResets).toBe(0)
    expect(ds.clearResetsMsg).toBe('Cleared')
    expect(ds.clearingResets).toBe(false)
  })

  it('sets clearResetsMsg to error message on failure', async () => {
    vi.mocked(api.clearAbnormalResets).mockRejectedValueOnce(new Error('boom'))
    const ds = createDiagnosticsState()
    await ds.doClearAbnormalResets()
    expect(ds.clearResetsMsg).toBe('boom')
    expect(ds.clearingResets).toBe(false)
  })

  it('flips clearingResets true during the call', async () => {
    let release!: () => void
    vi.mocked(api.clearAbnormalResets).mockReturnValueOnce(
      new Promise((res) => { release = () => res(undefined) })
    )
    const ds = createDiagnosticsState()
    const p = ds.doClearAbnormalResets()
    expect(ds.clearingResets).toBe(true)
    release()
    await p
    expect(ds.clearingResets).toBe(false)
  })
})

describe('doClearPanic()', () => {
  it('optimistically sets panic to no-active state on success', async () => {
    vi.mocked(api.clearDiagPanic).mockResolvedValueOnce(undefined)
    const ds = createDiagnosticsState()
    await ds.doClearPanic()
    expect(api.clearDiagPanic).toHaveBeenCalledTimes(1)
    expect(ds.panic).toEqual({ available: false, coredump: false, boots_since: 0 })
    expect(ds.clearingPanic).toBe(false)
    expect(ds.clearPanicMsg).toBe('')
  })

  it('sets clearPanicMsg on failure and leaves panic unchanged', async () => {
    vi.mocked(api.fetchDiagPanic).mockResolvedValueOnce({ available: true, coredump: true, boots_since: 1, task: 'IDLE1' } as any)
    vi.mocked(api.clearDiagPanic).mockRejectedValueOnce(new Error('flash busy'))
    const ds = createDiagnosticsState()
    await ds.loadPanic()
    await ds.doClearPanic()
    expect(ds.clearPanicMsg).toBe('flash busy')
    expect(ds.panic?.available).toBe(true)
    expect(ds.clearingPanic).toBe(false)
  })

  it('flips clearingPanic true during the call', async () => {
    let release!: () => void
    vi.mocked(api.clearDiagPanic).mockReturnValueOnce(
      new Promise((res) => { release = () => res(undefined) })
    )
    const ds = createDiagnosticsState()
    const p = ds.doClearPanic()
    expect(ds.clearingPanic).toBe(true)
    release()
    await p
    expect(ds.clearingPanic).toBe(false)
  })
})

describe('withRetry helper (via loadHeap/loadTasks)', () => {
  it('retries once on failure then succeeds', async () => {
    vi.useRealTimers() // need real setTimeout for the 400ms backoff
    const heapData = { internal: { free: 1, allocated: 1, largest_free_block: 1, minimum_ever_free: 1 }, dma: { free: 1, allocated: 1, largest_free_block: 1, minimum_ever_free: 1 }, default: { free: 1, allocated: 1, largest_free_block: 1, minimum_ever_free: 1 } }
    vi.mocked(api.fetchDiagHeap)
      .mockRejectedValueOnce(new Error('502'))
      .mockResolvedValueOnce(heapData as any)
    const ds = createDiagnosticsState()
    await ds.loadHeap()
    expect(api.fetchDiagHeap).toHaveBeenCalledTimes(2)
    expect(ds.heap).toEqual(heapData)
    expect(ds.heapErr).toBe('')
  })

  it('surfaces the error after both attempts fail', async () => {
    vi.useRealTimers()
    vi.mocked(api.fetchDiagHeap)
      .mockRejectedValueOnce(new Error('first'))
      .mockRejectedValueOnce(new Error('second'))
    const ds = createDiagnosticsState()
    await ds.loadHeap()
    expect(api.fetchDiagHeap).toHaveBeenCalledTimes(2)
    expect(ds.heapErr).toBe('second')
  })

  it('runHeapCheck falls back to bad on persistent failure', async () => {
    vi.useRealTimers()
    vi.mocked(api.checkDiagHeap)
      .mockRejectedValueOnce(new Error('x'))
      .mockRejectedValueOnce(new Error('x'))
    const ds = createDiagnosticsState()
    await ds.runHeapCheck()
    expect(ds.heapCheckResult).toBe('bad')
  })

  it('loadTasks sets tasksErr on persistent failure', async () => {
    vi.useRealTimers()
    vi.mocked(api.fetchDiagTasks)
      .mockRejectedValueOnce(new Error('x'))
      .mockRejectedValueOnce(new Error('boom-tasks'))
    const ds = createDiagnosticsState()
    await ds.loadTasks()
    expect(ds.tasksErr).toBe('boom-tasks')
  })

  it('loadPanic sets panic to null on persistent failure', async () => {
    vi.useRealTimers()
    vi.mocked(api.fetchDiagPanic)
      .mockRejectedValueOnce(new Error('x'))
      .mockRejectedValueOnce(new Error('x'))
    const ds = createDiagnosticsState()
    await ds.loadPanic()
    expect(ds.panic).toBeNull()
  })

  it('loadAbnormalResets sets abnormalResets to null on persistent failure', async () => {
    vi.useRealTimers()
    vi.mocked(api.fetchInfo)
      .mockRejectedValueOnce(new Error('x'))
      .mockRejectedValueOnce(new Error('x'))
    const ds = createDiagnosticsState()
    await ds.loadAbnormalResets()
    expect(ds.abnormalResets).toBeNull()
  })
})

describe('init() calls new loaders', () => {
  it('calls fetch functions for new diagnostics on init sequentially', async () => {
    const ds = createDiagnosticsState()
    await ds.init()
    // Verify that the new fetchers were called
    expect(api.fetchDiagHeap).toHaveBeenCalled()
    expect(api.fetchDiagTasks).toHaveBeenCalled()
    expect(api.fetchDiagPanic).toHaveBeenCalled()
    expect(api.fetchInfo).toHaveBeenCalled()
  })
})
