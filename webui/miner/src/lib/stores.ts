import { writable, get } from 'svelte/store'
import { fetchStats, fetchInfo, fetchSensors, fetchSettings, fetchPool, fetchHealth, type Stats, type Info, type Power, type Fan, type Thermal, type Settings, type Pool, type Health, type OtaCheckResult } from './api'

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
export const health = writable<Health | null>(null)
export const settings = writable<Settings | null>(null)
export const power = writable<Power | null>(null)
export const fan = writable<Fan | null>(null)
export const thermal = writable<Thermal | null>(null)
export const pool = writable<Pool | null>(null)
export const hasAsic = writable<boolean>(false)
export const connected = writable<boolean>(false)

// TA-315: single FanEditDialog mounted at app root; pages flip this to open.
export const fanEditOpen = writable<boolean>(false)

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
let settingsLoaded = false

/* Drop the cached /api/info + /api/settings so the next poll re-fetches them.
 * Call after a settings mutation so live config changes (display/LED on-off,
 * mDNS, etc.) propagate to read-only views like the System page without
 * continuous polling. Re-fetch (not optimistic) reflects reboot-pending state
 * truthfully. */
export function invalidateConfig() {
  infoLoaded = false
  settingsLoaded = false
}
/* Reboot detector: stats.uptime_s monotonically increases since boot, so a
 * drop means the device rebooted (counter restarted from ~0). When that
 * happens we invalidate the cached /api/info so the System page's Runtime
 * card (Reset reason, WDT resets, Last boot, build info after a rollback)
 * refreshes on the next poll. disc_age_s isn't usable here — it's "seconds
 * since last WiFi disconnect" and stays at 0 across a clean reboot. */
let lastUptimeS: number | null = null

async function poll() {
  try {
    const statsData = await fetchStats()
    stats.set(statsData)
    failCount = 0
    connected.set(true)

    /* Reboot detection. uptime_s drops on boot — when we see a drop, the
     * device restarted; refetch identity payloads so the System Runtime card
     * matches the new boot. The first sample (lastUptimeS === null) just
     * primes the watcher. */
    if (lastUptimeS !== null && statsData.uptime_s < lastUptimeS) {
      infoLoaded = false
      settingsLoaded = false
    }
    lastUptimeS = statsData.uptime_s

    // /api/sensors — unified fan/power/thermal/miner data (breadboard B1-269).
    // Transient failures leave stores at last value so UI sections don't flicker.
    let sensorsData: Awaited<ReturnType<typeof fetchSensors>> | null = null
    try {
      sensorsData = await fetchSensors()
    } catch {
      // keep prior store values on transient failure
    }
    if (sensorsData !== null) {
      const s = sensorsData
      // hasAsic: authoritative from /api/info capabilities (fetched below).
      // Derive from the info store once loaded; on first poll info may not be
      // loaded yet, so fall back to power.present from the sensors response.
      const infoVal = get(info)
      if (infoVal !== null) {
        hasAsic.set(infoVal.capabilities?.includes('asic') ?? false)
      } else {
        hasAsic.set(s.power?.present === true)
      }

      // Reconstruct the Power store shape: TM-relevant fields from miner;
      // vin_mv/present from power. All $power.* consumers keep working.
      const powerData: Power = {
        present: s.power?.present ?? undefined,
        vcore_mv: s.miner?.vcore_mv ?? null,
        icore_ma: s.miner?.icore_ma ?? null,
        pcore_mw: s.miner?.pcore_mw ?? null,
        efficiency_jth: s.miner?.efficiency_jth ?? null,
        efficiency_jth_1m: s.miner?.efficiency_jth_1m ?? null,
        efficiency_jth_10m: s.miner?.efficiency_jth_10m ?? null,
        efficiency_jth_1h: s.miner?.efficiency_jth_1h ?? null,
        expected_efficiency_jth: s.miner?.expected_efficiency_jth ?? null,
        vin_mv: s.power?.vin_mv ?? null,
        vin_low: s.miner?.vin_low ?? null,
        vr_temp_c: s.miner?.vr_temp_c ?? null,
      }
      power.set(powerData)
      fan.set(s.fan)
      thermal.set(s.thermal)
    }

    // /api/pool — TA-281; transient failures leave the store at last value.
    try {
      pool.set(await fetchPool())
    } catch {
      // keep prior value
    }

    // Append to rolling history buffer (session-local).
    const s = sensorsData
    const sample: HistorySample = {
      ts: Math.floor(Date.now() / 1000),
      total_ghs: statsData.asic_total_ghs ?? (statsData.hashrate ? statsData.hashrate / 1e9 : null),
      hw_err_pct: statsData.asic_hw_error_pct ?? null,
      temp_c: (s?.thermal?.asic.present ? s.thermal.asic.c : null) ?? statsData.temp_c ?? null,
      vr_temp_c: s?.miner?.vr_temp_c ?? null,
      board_temp_c: (s?.thermal?.board.present ? s.thermal.board.c : null) ?? null,
      pcore_w: s?.miner?.pcore_mw != null ? s.miner.pcore_mw / 1000 : null,
      vcore_v: s?.miner?.vcore_mv != null ? s.miner.vcore_mv / 1000 : null,
      efficiency_jth: s?.miner?.efficiency_jth ?? null,
      asic_freq_mhz: statsData.asic_freq_effective_mhz ?? null,
      rpm: s?.fan?.rpm ?? null,
      fan_duty: s?.fan?.duty_pct ?? null
    }
    history.update((buf) => {
      const next = buf.concat(sample)
      return next.length > HISTORY_MAX_SAMPLES ? next.slice(-HISTORY_MAX_SAMPLES) : next
    })

    try {
      health.set(await fetchHealth())
    } catch { /* keep prior value on transient failure */ }

    if (!infoLoaded) {
      try {
        const infoData = await fetchInfo()
        info.set(infoData)
        infoLoaded = true
        // Update hasAsic now that info is available — authoritative over the
        // power.present fallback used on the first poll.
        hasAsic.set(infoData.capabilities?.includes('asic') ?? false)
      } catch { /* transient; retry next cycle */ }
    }

    if (!settingsLoaded) {
      try {
        settings.set(await fetchSettings())
        settingsLoaded = true
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
  settingsLoaded = false
  thermal.set(null)
}
