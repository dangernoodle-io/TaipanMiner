import { writable } from 'svelte/store'
import { fetchStats, fetchInfo, fetchPower, fetchFan, type Stats, type Info, type Power, type Fan, type OtaCheckResult } from './api'

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
