import { writable } from 'svelte/store'
import { fetchStats, fetchInfo, fetchPower, fetchFan, type Stats, type Info, type Power, type Fan } from './api'

export const stats = writable<Stats | null>(null)
export const info = writable<Info | null>(null)
export const power = writable<Power | null>(null)
export const fan = writable<Fan | null>(null)
export const hasAsic = writable<boolean>(false)
export const connected = writable<boolean>(false)

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
