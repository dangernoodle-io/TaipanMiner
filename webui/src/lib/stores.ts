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

let pollInterval: ReturnType<typeof setInterval> | null = null
let failCount = 0
let infoLoaded = false

async function poll() {
  try {
    const statsData = await fetchStats()
    stats.set(statsData)
    failCount = 0
    connected.set(true)

    const [powerData, fanData] = await Promise.all([
      fetchPower().catch(() => null),
      fetchFan().catch(() => null)
    ])
    power.set(powerData)
    fan.set(fanData)
    hasAsic.set(powerData !== null)

    // Append to rolling history buffer (session-local).
    const sample: HistorySample = {
      ts: Math.floor(Date.now() / 1000),
      total_ghs: statsData.asic_total_ghs ?? (statsData.hw_hashrate ? statsData.hw_hashrate / 1e9 : null),
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
}
