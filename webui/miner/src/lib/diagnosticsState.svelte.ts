import {
  fetchDiagAsic, fetchLogLevels, setLogLevel, postReboot,
  fetchDiagHeap, checkDiagHeap, fetchDiagTasks, fetchDiagPanic, clearAbnormalResets, clearDiagPanic,
  fetchInfo, coredumpUrl,
  type LogLevel, type RecentDrop, type DiagHeap, type DiagTask, type DiagPanic,
} from './api'
import { startRebootRecovery } from './stores'
import { SseClient, type SseStatus } from './sse'
import { filterLines, retryInSeconds, findLevelForTag, type TagLevel } from './diagnosticsHelpers'

const LOG_MAX_LINES = 500

export function createDiagnosticsState() {
  const baseUrl = import.meta.env.VITE_MINER_URL ?? ''

  // ASIC telemetry
  let recentDrops = $state<RecentDrop[]>([])

  // SSE state
  let sse = $state.raw<SseClient | null>(null)
  let status = $state<SseStatus>('connecting')
  let wasDisconnected = $state(false)
  let nextRetryAt = $state<number | null>(null)
  let tickNow = $state(Date.now())
  let lines = $state<string[]>([])
  let autoscroll = $state(true)
  let filter = $state('')
  let panel = $state.raw<HTMLPreElement | null>(null)

  // Timers
  let tickTimer = $state.raw<ReturnType<typeof setInterval> | null>(null)
  let diagInterval = $state.raw<ReturnType<typeof setInterval> | null>(null)

  // Log level controls
  let availableLevels = $state<LogLevel[]>(['none', 'error', 'warn', 'info', 'debug', 'verbose'])
  let tagLevels = $state<TagLevel[]>([])
  let levelsLoading = $state(false)
  let levelsErr = $state('')
  let selectedTag = $state('')
  let selectedLevel = $state<LogLevel>('info')
  let applying = $state(false)
  let applyMsg = $state('')
  let applyKind = $state<'' | 'ok' | 'err'>('')

  // Reboot
  let rebooting = $state(false)
  let rebootMsg = $state('')
  let showRebootDialog = $state(false)

  // System health
  let heap = $state<DiagHeap | null>(null)
  let heapErr = $state('')
  let heapLoading = $state(false)
  let heapCheckResult = $state<'' | 'ok' | 'bad'>('')
  let heapChecking = $state(false)
  let tasks = $state<DiagTask[]>([])
  let tasksErr = $state('')
  let tasksLoading = $state(false)
  let panic = $state<DiagPanic | null>(null)
  let abnormalResets = $state<number | null>(null)
  let clearingResets = $state(false)
  let clearResetsMsg = $state('')
  let clearingPanic = $state(false)
  let clearPanicMsg = $state('')

  // Derived
  const retryInS = $derived(retryInSeconds(nextRetryAt, tickNow))
  const filtered = $derived(filterLines(lines, filter))
  const currentLevel = $derived(findLevelForTag(tagLevels, selectedTag))

  function syncSelectedLevel() {
    const cl = findLevelForTag(tagLevels, selectedTag)
    if (cl && !applying) selectedLevel = cl
  }

  async function loadDiagAsic() {
    try {
      const data = await withRetry(fetchDiagAsic)
      recentDrops = data.recent_drops
    } catch {
      recentDrops = []
    }
  }

  async function loadLevels() {
    levelsLoading = true
    levelsErr = ''
    try {
      const data = await withRetry(fetchLogLevels)
      availableLevels = [...data.levels].sort((a, b) => a.localeCompare(b))
      tagLevels = data.tags.map((t) => ({ ...t })).sort((a, b) => a.tag.localeCompare(b.tag))
      if (!selectedTag && tagLevels.length) selectedTag = tagLevels[0].tag
      syncSelectedLevel()
    } catch (e) {
      levelsErr = (e as Error).message
    } finally {
      levelsLoading = false
    }
  }

  async function applyLevel() {
    if (!selectedTag) return
    applying = true
    applyMsg = ''
    applyKind = ''
    try {
      await setLogLevel(selectedTag, selectedLevel)
      const idx = tagLevels.findIndex((t) => t.tag === selectedTag)
      if (idx >= 0) {
        tagLevels[idx] = { ...tagLevels[idx], level: selectedLevel }
        tagLevels = tagLevels
      }
      applyKind = 'ok'
      applyMsg = `${selectedTag} → ${selectedLevel}`
    } catch (e) {
      applyKind = 'err'
      applyMsg = (e as Error).message
    } finally {
      applying = false
    }
  }

  async function doReboot() {
    rebooting = true
    rebootMsg = ''
    try {
      await postReboot()
      startRebootRecovery('Rebooting miner')
    } catch (e) {
      rebootMsg = `Reboot failed: ${(e as Error).message}`
    } finally {
      rebooting = false
    }
  }

  const REBOOT_SKIP_KEY = 'taipanminer.skipRebootConfirm'

  function requestReboot() {
    const skip = (() => {
      try { return localStorage.getItem(REBOOT_SKIP_KEY) === '1' } catch { return false }
    })()
    if (skip) {
      doReboot()
    } else {
      showRebootDialog = true
    }
  }

  function cancelReboot() {
    showRebootDialog = false
  }

  function clear() {
    lines = []
  }

  function onPanelScroll() {
    if (!panel) return
    const atBottom = panel.scrollHeight - panel.scrollTop - panel.clientHeight < 8
    autoscroll = atBottom
  }

  function startStream() {
    sse = new SseClient({
      url: `${baseUrl}/api/logs?source=browser`,
      onOpen: () => {
        if (wasDisconnected) {
          wasDisconnected = false
          // Device may have rebooted — re-query tag list (levels reset on reboot).
          loadLevels()
        }
      },
      onMessage: (data) => {
        lines = lines.concat(data)
        if (lines.length > LOG_MAX_LINES) lines = lines.slice(-LOG_MAX_LINES)
        if (autoscroll) {
          queueMicrotask(() => {
            if (panel) panel.scrollTop = panel.scrollHeight
          })
        }
      },
      onStatusChange: (s) => {
        status = s
        if (s === 'disconnected' || s === 'external') wasDisconnected = true
      },
      onRetryAtChange: (at) => { nextRetryAt = at },
      resolveErrorStatus: async () => {
        try {
          const r = await fetch(`${baseUrl}/api/logs/status`)
          const d: { active: boolean; client: string } = await r.json()
          return d.active && d.client === 'external' ? 'external' : 'disconnected'
        } catch {
          return 'disconnected'
        }
      },
    })
    sse.start()
  }

  function onVisibilityChange() {
    if (document.visibilityState !== 'visible') return
    /* Tab was hidden long enough to stall — reconnect immediately rather
     * than waiting for the next 5s stall-check tick. */
    if (sse?.isStale()) sse.reconnectNow()
  }

  function onLevelChange(e: Event) {
    const target = e.currentTarget as HTMLSelectElement
    selectedLevel = target.value as LogLevel
    applyLevel()
  }

  // Single retry on transient httpd/proxy failures. The dev server's
  // proxy can surface 502s when the device is briefly busy serving
  // another request; one retry after a small delay smooths that without
  // hiding genuinely broken endpoints.
  async function withRetry<T>(fn: () => Promise<T>): Promise<T> {
    try {
      return await fn()
    } catch {
      await new Promise((r) => setTimeout(r, 400))
      return await fn()
    }
  }

  async function loadHeap() {
    heapLoading = true
    heapErr = ''
    try {
      heap = await withRetry(fetchDiagHeap)
    } catch (e) {
      heapErr = (e as Error).message
    } finally {
      heapLoading = false
    }
  }

  async function runHeapCheck() {
    heapChecking = true
    heapCheckResult = ''
    try {
      heapCheckResult = (await withRetry(checkDiagHeap)) ? 'ok' : 'bad'
    } catch {
      heapCheckResult = 'bad'
    } finally {
      heapChecking = false
    }
  }

  async function loadTasks() {
    tasksLoading = true
    tasksErr = ''
    try {
      const t = await withRetry(fetchDiagTasks)
      tasks = t.slice().sort((a, b) => b.prio - a.prio || a.name.localeCompare(b.name))
    } catch (e) {
      tasksErr = (e as Error).message
    } finally {
      tasksLoading = false
    }
  }

  async function loadPanic() {
    try { panic = await withRetry(fetchDiagPanic) } catch { panic = null }
  }

  async function loadAbnormalResets() {
    try {
      const info = await withRetry(fetchInfo)
      abnormalResets = (info as unknown as { abnormal_reset_count?: number }).abnormal_reset_count ?? null
    } catch {
      abnormalResets = null
    }
  }

  async function doClearAbnormalResets() {
    clearingResets = true
    clearResetsMsg = ''
    try {
      await clearAbnormalResets()
      abnormalResets = 0
      clearResetsMsg = 'Cleared'
    } catch (e) {
      clearResetsMsg = (e as Error).message
    } finally {
      clearingResets = false
    }
  }

  async function doClearPanic() {
    clearingPanic = true
    clearPanicMsg = ''
    try {
      await clearDiagPanic()
      panic = { available: false, coredump: false, boots_since: 0 }
    } catch (e) {
      clearPanicMsg = (e as Error).message
    } finally {
      clearingPanic = false
    }
  }

  async function init() {
    startStream()  // SSE is long-lived; doesn't compete with sequential burst

    await loadDiagAsic()
    await loadLevels()
    await loadHeap()
    await loadTasks()
    await loadPanic()
    await loadAbnormalResets()

    diagInterval = setInterval(loadDiagAsic, 10000)
    tickTimer = setInterval(() => { tickNow = Date.now() }, 1000)
    document.addEventListener('visibilitychange', onVisibilityChange)
  }

  function destroy() {
    document.removeEventListener('visibilitychange', onVisibilityChange)
    if (diagInterval !== null) clearInterval(diagInterval)
    if (tickTimer !== null) clearInterval(tickTimer)
    sse?.destroy()
    sse = null
  }

  return {
    // State getters
    get recentDrops() { return recentDrops },
    get status() { return status },
    get wasDisconnected() { return wasDisconnected },
    get nextRetryAt() { return nextRetryAt },
    get tickNow() { return tickNow },
    get lines() { return lines },
    get autoscroll() { return autoscroll },
    set autoscroll(v) { autoscroll = v },
    get filter() { return filter },
    set filter(v) { filter = v },
    get panel() { return panel },
    set panel(v) { panel = v },

    // Derived
    get retryInS() { return retryInS },
    get filtered() { return filtered },
    get currentLevel() { return currentLevel },

    // Log level controls
    get availableLevels() { return availableLevels },
    get tagLevels() { return tagLevels },
    get levelsLoading() { return levelsLoading },
    get levelsErr() { return levelsErr },
    get selectedTag() { return selectedTag },
    set selectedTag(v) { selectedTag = v; syncSelectedLevel() },
    get selectedLevel() { return selectedLevel },
    set selectedLevel(v) { selectedLevel = v },
    get applying() { return applying },
    get applyMsg() { return applyMsg },
    get applyKind() { return applyKind },

    // Reboot
    get rebooting() { return rebooting },
    get rebootMsg() { return rebootMsg },
    get showRebootDialog() { return showRebootDialog },
    set showRebootDialog(v) { showRebootDialog = v },

    // System health
    get heap() { return heap },
    get heapErr() { return heapErr },
    get heapLoading() { return heapLoading },
    get heapCheckResult() { return heapCheckResult },
    get heapChecking() { return heapChecking },
    get tasks() { return tasks },
    get tasksErr() { return tasksErr },
    get tasksLoading() { return tasksLoading },
    get panic() { return panic },
    get abnormalResets() { return abnormalResets },
    get clearingResets() { return clearingResets },
    get clearResetsMsg() { return clearResetsMsg },
    get clearingPanic() { return clearingPanic },
    get clearPanicMsg() { return clearPanicMsg },
    coredumpUrl,

    // Constants
    REBOOT_SKIP_KEY,

    // Actions
    init,
    destroy,
    loadDiagAsic,
    loadLevels,
    applyLevel,
    onLevelChange,
    doReboot,
    requestReboot,
    cancelReboot,
    clear,
    onPanelScroll,
    startStream,
    onVisibilityChange,
    loadHeap,
    runHeapCheck,
    loadTasks,
    loadPanic,
    loadAbnormalResets,
    doClearAbnormalResets,
    doClearPanic,
  }
}
