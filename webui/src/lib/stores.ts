import { writable, get } from 'svelte/store'
import { fetchStats, fetchInfo, fetchPower, fetchFan, type Stats, type Info, type Power, type Fan, type OtaCheckResult } from './api'

export interface HistorySample {
  ts: number              // epoch seconds (client-side)
  total_ghs: number | null
  hw_err_pct: number | null
  temp_c: number | null        // ASIC die temp
  vr_temp_c: number | null
  board_temp_c: number | null
  pcore_w: number | null
  vcore_v: number | null
  efficiency_jth: number | null
  asic_freq_mhz: number | null
  rpm: number | null
  fan_duty: number | null
}

const HISTORY_MAX_SAMPLES = 1440 // 2 hours at 5s cadence
export const history = writable<HistorySample[]>([])

export const stats = writable<Stats | null>(null)
export const info = writable<Info | null>(null)
export const power = writable<Power | null>(null)
export const fan = writable<Fan | null>(null)
export const hasAsic = writable<boolean>(false)
export const connected = writable<boolean>(false)

// OTA UI state — persists across page navigations so a check result and install
// progress remain visible when returning to the Update page.
export type OtaKind = '' | 'ok' | 'avail' | 'err'

export const otaCheck = writable<{
  checking: boolean
  result: OtaCheckResult | null
  msg: string
  kind: OtaKind
}>({ checking: false, result: null, msg: '', kind: '' })

export const otaInstall = writable<{
  installing: boolean
  pct: number
  state: string
  msg: string
  kind: OtaKind
}>({ installing: false, pct: 0, state: '', msg: '', kind: '' })

export const otaUpload = writable<{
  uploading: boolean
  pct: number
  msg: string
  kind: OtaKind
}>({ uploading: false, pct: 0, msg: '', kind: '' })

// Reboot overlay — UI enters this state after a user-initiated reboot or
// firmware flash, exits once the device responds to a liveness probe.
export const rebooting = writable<{
  active: boolean
  reason: string
  elapsed: number
  timedOut: boolean
}>({ active: false, reason: '', elapsed: 0, timedOut: false })

import { ping as apiPing } from './api'

let rebootPollId: ReturnType<typeof setInterval> | null = null
let rebootStartTs = 0

/**
 * Signal that the miner is restarting (after a reboot or flash). The UI
 * renders an overlay; this function polls /api/version every second and
 * resolves it once the device responds, or times out after 90s.
 */
export function startRebootRecovery(reason: string) {
  if (rebootPollId) return
  rebootStartTs = Date.now()
  rebooting.set({ active: true, reason, elapsed: 0, timedOut: false })
  // Wait 3s before polling — the device usually hasn't even stopped yet.
  setTimeout(() => {
    rebootPollId = setInterval(async () => {
      const elapsed = Math.floor((Date.now() - rebootStartTs) / 1000)
      if (elapsed > 90) {
        rebooting.update((s) => ({ ...s, elapsed, timedOut: true }))
        return
      }
      const alive = await apiPing(1500)
      if (alive) {
        clearInterval(rebootPollId!)
        rebootPollId = null
        rebooting.set({ active: false, reason: '', elapsed: 0, timedOut: false })
        // Clear OTA state — the upgrade flow is done; the page should be back
        // to a fresh "no current operation" view, not pinned at "Install complete".
        otaCheck.set({ checking: false, result: null, msg: '', kind: '' })
        otaInstall.set({ installing: false, pct: 0, state: '', msg: '', kind: '' })
        otaUpload.set({ uploading: false, pct: 0, msg: '', kind: '' })
        return
      }
      rebooting.update((s) => ({ ...s, elapsed }))
    }, 1000)
  }, 3000)
}

// Test helper to reset reboot state
export function __resetRebootPoll() {
  if (rebootPollId !== null) {
    clearInterval(rebootPollId)
  }
  rebootPollId = null
}

let pollInterval: ReturnType<typeof setInterval> | null = null
let failCount = 0
let infoLoaded = false
let asicProbed = false
let asicAvailable = false

async function poll() {
  try {
    const statsData = await fetchStats()
    stats.set(statsData)
    failCount = 0
    connected.set(true)

    // Probe /api/power once to detect ASIC capability. Subsequent polls only
    // hit /api/power and /api/fan on ASIC boards — keeps tdongle firmware
    // logs clean of 405 warnings from missing handlers.
    let powerData: Power | null = null
    let fanData: Fan | null = null
    if (!asicProbed) {
      powerData = await fetchPower().catch(() => null)
      asicAvailable = powerData !== null
      asicProbed = true
      hasAsic.set(asicAvailable)
      if (asicAvailable) {
        fanData = await fetchFan().catch(() => null)
      }
    } else if (asicAvailable) {
      [powerData, fanData] = await Promise.all([
        fetchPower().catch(() => null),
        fetchFan().catch(() => null)
      ])
    }
    power.set(powerData)
    fan.set(fanData)

    // Append to rolling history buffer (session-local).
    const sample: HistorySample = {
      ts: Math.floor(Date.now() / 1000),
      total_ghs: statsData.asic_total_ghs ?? (statsData.hashrate ? statsData.hashrate / 1e9 : null),
      hw_err_pct: statsData.asic_hw_error_pct ?? null,
      temp_c: statsData.asic_temp_c ?? statsData.temp_c ?? null,
      vr_temp_c: powerData?.vr_temp_c ?? null,
      board_temp_c: powerData?.board_temp_c ?? null,
      pcore_w: powerData?.pcore_mw != null ? powerData.pcore_mw / 1000 : null,
      vcore_v: powerData?.vcore_mv != null ? powerData.vcore_mv / 1000 : null,
      efficiency_jth: powerData?.efficiency_jth ?? null,
      asic_freq_mhz: statsData.asic_freq_effective_mhz ?? null,
      rpm: fanData?.rpm ?? null,
      fan_duty: fanData?.duty_pct ?? null
    }
    history.update((buf) => {
      const next = buf.concat(sample)
      return next.length > HISTORY_MAX_SAMPLES ? next.slice(-HISTORY_MAX_SAMPLES) : next
    })

    if (!infoLoaded) {
      try {
        info.set(await fetchInfo())
        infoLoaded = true
      } catch { /* transient; retry next cycle */ }
    }
  } catch {
    failCount++
    if (failCount >= 2) connected.set(false)
  }
}

export function start() {
  if (pollInterval) return
  poll()
  pollInterval = setInterval(poll, 5000)
}

export function stop() {
  if (pollInterval) {
    clearInterval(pollInterval)
    pollInterval = null
  }
  infoLoaded = false
  asicProbed = false
  asicAvailable = false
}
