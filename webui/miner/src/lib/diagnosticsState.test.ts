import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'

vi.mock('./api', () => ({
  fetchDiagAsic: vi.fn().mockResolvedValue({ recent_drops: [] }),
  fetchLogLevels: vi.fn().mockResolvedValue({ levels: [], tags: [] }),
  setLogLevel: vi.fn().mockResolvedValue(undefined),
  postReboot: vi.fn().mockResolvedValue(undefined),
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
  it('calls loadDiagAsic immediately', () => {
    const ds = createDiagnosticsState()
    ds.init()
    expect(api.fetchDiagAsic).toHaveBeenCalledTimes(1)
  })

  it('calls loadLevels on init', () => {
    const ds = createDiagnosticsState()
    ds.init()
    expect(api.fetchLogLevels).toHaveBeenCalledTimes(1)
  })

  it('creates and starts SseClient', () => {
    const ds = createDiagnosticsState()
    ds.init()
    expect(lastSseInstance()).not.toBeNull()
    expect(lastSseInstance().started).toBe(true)
  })

  it('sets up diagInterval that re-polls every 10s', () => {
    const ds = createDiagnosticsState()
    ds.init()
    vi.advanceTimersByTime(10000)
    // 1 on init + 1 from interval
    expect(api.fetchDiagAsic).toHaveBeenCalledTimes(2)
  })

  it('sets up tickTimer that fires every 1s', () => {
    const ds = createDiagnosticsState()
    const before = ds.tickNow
    ds.init()
    vi.advanceTimersByTime(1100)
    // tickNow should have advanced
    expect(ds.tickNow).toBeGreaterThan(before)
  })

  it('adds visibilitychange listener', () => {
    const ds = createDiagnosticsState()
    const spy = vi.spyOn(document, 'addEventListener')
    ds.init()
    expect(spy).toHaveBeenCalledWith('visibilitychange', expect.any(Function))
  })
})

describe('destroy()', () => {
  it('removes visibilitychange listener', () => {
    const ds = createDiagnosticsState()
    ds.init()
    const spy = vi.spyOn(document, 'removeEventListener')
    ds.destroy()
    expect(spy).toHaveBeenCalledWith('visibilitychange', expect.any(Function))
  })

  it('clears diagInterval', () => {
    const ds = createDiagnosticsState()
    ds.init()
    ds.destroy()
    // After destroy, polling should stop
    vi.advanceTimersByTime(20000)
    // Only the 1 initial call from init(), no more
    expect(api.fetchDiagAsic).toHaveBeenCalledTimes(1)
  })

  it('destroys the sse client', () => {
    const ds = createDiagnosticsState()
    ds.init()
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

  it('sets recentDrops to empty on error', async () => {
    vi.mocked(api.fetchDiagAsic).mockRejectedValueOnce(new Error('network'))
    const ds = createDiagnosticsState()
    await ds.loadDiagAsic()
    expect(ds.recentDrops).toEqual([])
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

  it('sets levelsErr on error', async () => {
    vi.mocked(api.fetchLogLevels).mockRejectedValueOnce(new Error('timeout'))
    const ds = createDiagnosticsState()
    await ds.loadLevels()
    expect(ds.levelsErr).toBe('timeout')
    expect(ds.levelsLoading).toBe(false)
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
  it('onMessage appends to lines', () => {
    const ds = createDiagnosticsState()
    ds.init()
    lastSseInstance().callbacks.onMessage('hello')
    lastSseInstance().callbacks.onMessage('world')
    expect(ds.lines).toEqual(['hello', 'world'])
  })

  it('onMessage caps lines at 500', () => {
    const ds = createDiagnosticsState()
    ds.init()
    // Push 501 lines
    for (let i = 0; i < 501; i++) {
      lastSseInstance().callbacks.onMessage(`line${i}`)
    }
    expect(ds.lines).toHaveLength(500)
    expect(ds.lines[0]).toBe('line1') // first line dropped
    expect(ds.lines[499]).toBe('line500')
  })

  it('onStatusChange updates status', () => {
    const ds = createDiagnosticsState()
    ds.init()
    lastSseInstance().callbacks.onStatusChange('connected')
    expect(ds.status).toBe('connected')
  })

  it('onStatusChange sets wasDisconnected on disconnected', () => {
    const ds = createDiagnosticsState()
    ds.init()
    expect(ds.wasDisconnected).toBe(false)
    lastSseInstance().callbacks.onStatusChange('disconnected')
    expect(ds.wasDisconnected).toBe(true)
  })

  it('onStatusChange sets wasDisconnected on external', () => {
    const ds = createDiagnosticsState()
    ds.init()
    lastSseInstance().callbacks.onStatusChange('external')
    expect(ds.wasDisconnected).toBe(true)
  })

  it('onRetryAtChange updates nextRetryAt', () => {
    const ds = createDiagnosticsState()
    ds.init()
    const at = Date.now() + 3000
    lastSseInstance().callbacks.onRetryAtChange(at)
    expect(ds.nextRetryAt).toBe(at)
  })

  it('onRetryAtChange accepts null', () => {
    const ds = createDiagnosticsState()
    ds.init()
    lastSseInstance().callbacks.onRetryAtChange(null)
    expect(ds.nextRetryAt).toBeNull()
  })

  it('onOpen reloads levels when wasDisconnected', async () => {
    const ds = createDiagnosticsState()
    ds.init()
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
    ds.init()
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
  it('does nothing when document is hidden', () => {
    Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'hidden' })
    const ds = createDiagnosticsState()
    ds.init()
    ds.onVisibilityChange()
    expect(lastSseInstance().reconnectNow).not.toHaveBeenCalled()
  })

  it('calls reconnectNow when visible and sse is stale', () => {
    Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'visible' })
    const ds = createDiagnosticsState()
    ds.init()
    lastSseInstance().isStale.mockReturnValue(true)
    ds.onVisibilityChange()
    expect(lastSseInstance().reconnectNow).toHaveBeenCalled()
  })

  it('does not reconnect when visible but not stale', () => {
    Object.defineProperty(document, 'visibilityState', { configurable: true, get: () => 'visible' })
    const ds = createDiagnosticsState()
    ds.init()
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
