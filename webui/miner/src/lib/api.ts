export interface RecentDrop {
  ts_ago_s: number
  chip: number
  kind: 'total' | 'error' | 'domain'
  domain: number
  addr: number
  ghs: number
  delta: number
  elapsed_s: number
}

export interface Chip {
  idx: number
  total_ghs: number
  error_ghs: number
  hw_err_pct: number
  total_raw: number
  error_raw: number
  domain_ghs: number[]
  total_drops: number
  error_drops: number
  domain_drops: number[]
  // TA-237: seconds since most-recent telemetry drop on this chip; null if never
  last_drop_ago_s: number | null
}

export interface Stats {
  session_shares: number
  session_rejected: number
  session_blocks_found?: number
  /* Wall-clock unix seconds; 0 (or missing) = unset (no block this session,
   * or SNTP not yet synced when the event fired). */
  session_best_diff_ts?: number
  session_last_block_ts?: number
  // Lifetime aggregates moved to /api/pool's stats[] array (per-pool slots);
  // the headline "lifetime" number in the UI is derived client-side.
  last_share_ago_s: number | null
  best_diff: number
  uptime_s: number
  temp_c: number
  hashrate: number
  hashrate_avg: number
  shares: number | null
  asic_hashrate: number | null
  asic_hashrate_avg: number | null
  asic_shares: number | null
  asic_freq_configured_mhz: number | null
  asic_freq_effective_mhz: number | null
  asic_small_cores: number | null
  asic_count: number | null
  expected_ghs: number | null
  asic_total_ghs: number | null
  asic_hw_error_pct: number | null
  asic_total_ghs_1m: number | null
  asic_total_ghs_10m: number | null
  asic_total_ghs_1h: number | null
  asic_hw_error_pct_1m: number | null
  asic_hw_error_pct_10m: number | null
  asic_hw_error_pct_1h: number | null
  hashrate_1m: number | null
  hashrate_10m: number | null
  hashrate_1h: number | null
  hw_error_pct_1m: number | null
  hw_error_pct_10m: number | null
  hw_error_pct_1h: number | null
  pool_effective_hashrate: number | null
  asic_chips?: Chip[]
  rejected?: RejectedBreakdown
}

export interface RejectedBreakdown {
  total: number
  job_not_found: number
  low_difficulty: number
  duplicate: number
  stale_prevhash: number
  other: number
  other_last_code: number
}

export interface InfoNetwork {
  ssid: string | null
  bssid: string | null
  rssi: number | null
  ip: string | null
  connected: boolean
  disc_reason: number
  disc_age_s: number
  retry_count: number
  mdns: boolean
  stratum: boolean
  stratum_reconnect_ms: number
  stratum_fail_count: number
}

export interface Info {
  board: string
  project_name: string
  version: string
  idf_version: string
  build_date: string
  build_time: string
  chip_model: string
  cores: number
  mac: string
  ssid: string | null
  flash_size: number
  app_size: number
  total_heap: number
  free_heap: number
  reset_reason: string | null
  wdt_resets: number | null
  boot_time: number | null
  worker_name: string
  hostname: string
  validated: boolean
  network?: InfoNetwork
}

/**
 * /api/health — live liveness signals, polled every 5s.
 *
 * disc_age_s is monotonic since the last WiFi disconnect; it resets to 0
 * when the device reconnects post-reboot, so a sharp drop is the cheap
 * reboot detector that lets us refetch /api/info.
 */
export interface HealthNetwork {
  connected: boolean
  rssi: number
  disc_age_s: number
  retry_count: number
  mdns: string | null
  stratum?: boolean              // TaipanMiner extender
  stratum_fail_count?: number    // TaipanMiner extender
  knot?: boolean                 // TaipanMiner extender
}

export interface Health {
  ok: boolean
  free_heap: number
  validated: boolean
  network: HealthNetwork
  sha_self_test_failed?: boolean
}

export interface ThermalSensor {
  present: boolean
  c: number | null
}

export interface Thermal {
  soc: ThermalSensor
  vr: ThermalSensor
  asic: ThermalSensor
  board: ThermalSensor
}

export interface Power {
  vcore_mv: number | null
  icore_ma: number | null
  pcore_mw: number | null
  pout_mw?: number | null
  efficiency_jth: number | null
  efficiency_jth_1m: number | null
  efficiency_jth_10m: number | null
  efficiency_jth_1h: number | null
  expected_efficiency_jth: number | null
  vin_mv: number | null
  vin_low: boolean | null
  board_temp_c: number | null
  vr_temp_c: number | null
}

export interface Fan {
  rpm: number | null
  duty_pct: number | null
  autofan: boolean
  die_target_c: number
  vr_target_c: number
  manual_pct: number
  min_pct: number
  // TA-141 telemetry
  die_ema_c: number | null
  vr_ema_c: number | null
  pid_input_c: number | null
  pid_input_src: 'die' | 'vr'
}

export interface FanPatch {
  autofan?: boolean
  die_target_c?: number
  vr_target_c?: number
  manual_pct?: number
  min_pct?: number
}

// POST /api/fan — form-urlencoded, partial. Strict server-side parsing
// rejects malformed values with 400.
export async function patchFan(body: FanPatch): Promise<void> {
  const params = new URLSearchParams()
  for (const [k, v] of Object.entries(body)) {
    if (v === undefined) continue
    params.set(k, String(v))
  }
  const res = await fetch(`${baseUrl}/api/fan`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: params.toString()
  })
  if (!res.ok) throw new Error(`fan patch failed: ${res.status}`)
}

export interface KnotPeer {
  instance: string
  hostname: string
  ip: string
  worker: string
  board: string
  version: string
  state: string
  seen_ago_s: number
  ui: boolean
}

// In dev, Vite proxies /api/* to VITE_MINER_URL (configured in vite.config.ts).
// In production embed, the UI is same-origin with the firmware, so an empty
// baseUrl keeps fetches relative.
const baseUrl = ''

async function getJson<T>(path: string): Promise<T> {
  const res = await fetch(`${baseUrl}${path}`)
  if (!res.ok) throw new Error(`${path} failed: ${res.status}`)
  return res.json() as Promise<T>
}

// TA-281/TA-286: locked /api/pool shape — pool config + session-scoped
// negotiated values + most-recent stratum mining.notify.
export interface PoolNotify {
  job_id: string
  prev_hash: string
  coinb1: string
  coinb2: string
  merkle_branches: string[]
  version: string      // 8-char hex
  nbits: string        // 8-char hex
  ntime: string        // 8-char hex
  clean_jobs: boolean
}

export interface PoolConfigured {
  host: string
  port: number
  worker: string
  wallet: string
  // TA-306: send mining.extranonce.subscribe after authorize.
  extranonce_subscribe: boolean
  // TA-307: UI decodes coinbase tx fields (height, scriptSig tag, payout, reward).
  decode_coinbase: boolean
}

export interface PoolStat {
  host: string
  port: number
  shares: number
  hashes: number
  best_diff: number
  blocks_found: number
  last_seen_s: number
  /* Wall-clock unix seconds; 0 = unset (best_diff still at default, no block
   * found at this pool, or SNTP not yet synced when the event fired). */
  best_diff_ts?: number
  last_block_ts?: number
}

export interface Pool {
  host: string
  port: number
  worker: string
  wallet: string
  connected: boolean
  session_start_ago_s: number | null
  current_difficulty: number
  pool_effective_hashrate: number | null
  pool_effective_hashrate_1m: number | null
  pool_effective_hashrate_10m: number | null
  pool_effective_hashrate_1h: number | null
  latency_ms: number | null
  extranonce1: string | null
  extranonce2_size: number | null
  version_mask: string | null
  notify: PoolNotify | null
  active_pool_idx: 0 | 1 | null
  extranonce_subscribe_status: 'off' | 'pending' | 'active' | 'rejected'
  configured: {
    primary: PoolConfigured | null
    fallback: PoolConfigured | null
  }
  stats?: PoolStat[]
  /* Device-lifetime block counter — never reset on LRU pool eviction. */
  lifetime_blocks_total?: number
  /* Wall-clock unix seconds of the most recent device-lifetime block find.
   * 0 = unset. */
  lifetime_last_block_ts?: number
}

export interface PoolConfigInput {
  host: string
  port: number
  worker: string
  wallet: string
  pool_pass: string
  // Optional on PUT — omitting preserves the current value.
  extranonce_subscribe?: boolean
  decode_coinbase?: boolean
}

export interface PoolPutBody {
  primary: PoolConfigInput
  fallback: PoolConfigInput | null
}

export const fetchPool = () => getJson<Pool>('/api/pool')

export interface DiagAsic { recent_drops: RecentDrop[] }
export const fetchDiagAsic = () => getJson<DiagAsic>('/api/diag/asic')

export interface HeapCap {
  free: number
  allocated: number
  largest_free_block: number
  minimum_ever_free: number
}
export interface DiagHeap {
  internal: HeapCap
  dma: HeapCap
  default: HeapCap
}
export const fetchDiagHeap = () => getJson<DiagHeap>('/api/diag/heap')

export async function checkDiagHeap(): Promise<boolean> {
  const res = await fetch(`${baseUrl}/api/diag/heap?check=true`)
  if (!res.ok) throw new Error(`heap check failed: ${res.status}`)
  const d = await res.json() as { integrity_ok: boolean }
  return d.integrity_ok
}

export type TaskState = 'running' | 'ready' | 'blocked' | 'suspended' | 'deleted' | 'invalid'
export interface DiagTask {
  name: string
  prio: number
  base_prio: number
  stack_hwm: number
  state: TaskState
}
export const fetchDiagTasks = () => getJson<DiagTask[]>('/api/diag/tasks')

export interface DiagPanic {
  available: boolean
  coredump: boolean
  boots_since: number
  task?: string
  exc_pc?: number
  exc_cause?: number
  backtrace?: number[]
  panic_reason?: string
}
export const fetchDiagPanic = () => getJson<DiagPanic>('/api/diag/panic')

export interface DiagBootPanic {
  available: boolean
  boots_since?: number
  reset_reason?: string
}
export interface DiagBoot {
  reset_reason: string
  abnormal_reset_count: number
  panic: DiagBootPanic
}
export const fetchDiagBoot = () => getJson<DiagBoot>('/api/diag/boot')

// DELETE /api/diag/boot — clears panic log + abnormal-reset counter in one call
// (replaces the removed DELETE /api/diag/panic + DELETE /api/diag/abnormal-resets)
export async function clearDiagBoot(): Promise<void> {
  const res = await fetch(`${baseUrl}/api/diag/boot`, { method: 'DELETE' })
  if (!res.ok) throw new Error(`clear diag boot failed: ${res.status}`)
}

// Kept for API compat: both operations now collapse to a single DELETE /api/diag/boot
export const clearAbnormalResets = clearDiagBoot
export const clearDiagPanic      = clearDiagBoot

export const coredumpUrl = `${baseUrl}/api/diag/coredump`

export interface Settings {
  hostname: string
  display_en?: boolean
  ota_skip_check?: boolean
  mdns_en?: boolean
  knot_en?: boolean
  led_heartbeat_en?: boolean
}

export const fetchStats    = () => getJson<Stats>('/api/stats')
export const fetchInfo     = () => getJson<Info>('/api/info')
export const fetchHealth   = () => getJson<Health>('/api/health')
export const fetchPower    = () => getJson<Power>('/api/power')
export const fetchFan      = () => getJson<Fan>('/api/fan')
export const fetchThermal  = () => getJson<Thermal>('/api/thermal')
export const fetchSettings = () => getJson<Settings>('/api/settings')
export const fetchKnot = () => getJson<KnotPeer[]>('/api/knot')

export async function patchSettings(patch: Partial<Settings>): Promise<{ status: string; reboot_required: boolean }> {
  const res = await fetch(`${baseUrl}/api/settings`, {
    method: 'PATCH',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(patch)
  })
  if (!res.ok) throw new Error(`settings patch failed: ${res.status}`)
  return res.json()
}

export async function putPool(body: PoolPutBody): Promise<void> {
  const res = await fetch(`${baseUrl}/api/pool`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  })
  if (!res.ok) throw new Error(`pool put failed: ${res.status}`)
}

export async function switchPool(idx: 0 | 1): Promise<void> {
  const res = await fetch(`${baseUrl}/api/pool/switch`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ idx })
  })
  if (!res.ok) throw new Error(`switch pool failed: ${res.status}`)
}

// DELETE /api/pool/fallback — clear fallback slot.
// DELETE /api/pool/primary  — promote fallback to primary; 409 if no fallback.
export async function deletePoolSlot(slot: 'primary' | 'fallback'): Promise<void> {
  const res = await fetch(`${baseUrl}/api/pool/${slot}`, { method: 'DELETE' })
  if (!res.ok) {
    const body = await res.text().catch(() => '')
    throw new Error(body || `delete pool ${slot} failed: ${res.status}`)
  }
}

export async function postReboot(): Promise<void> {
  const res = await fetch(`${baseUrl}/api/reboot`, { method: 'POST' })
  if (!res.ok) throw new Error(`reboot failed: ${res.status}`)
}

export async function resetStats(): Promise<void> {
  const res = await fetch(`${baseUrl}/api/stats/reset`, { method: 'POST' })
  if (!res.ok) throw new Error(`reset stats failed: ${res.status}`)
}

// Lightweight liveness probe. Polls /api/health — any 200 means the device is up.
export async function ping(timeoutMs = 2000): Promise<boolean> {
  const ctrl = new AbortController()
  const t = setTimeout(() => ctrl.abort(), timeoutMs)
  try {
    const res = await fetch(`${baseUrl}/api/health`, { signal: ctrl.signal })
    return res.ok
  } catch {
    return false
  } finally {
    clearTimeout(t)
  }
}

export type LogLevel = 'error' | 'warn' | 'info' | 'debug' | 'verbose' | 'none'

export interface LogLevelList {
  levels: LogLevel[]
  tags: { tag: string; level: LogLevel }[]
}

export const fetchLogLevels = () => getJson<LogLevelList>('/api/log/level')

export async function setLogLevel(tag: string, level: LogLevel): Promise<void> {
  const body = `tag=${encodeURIComponent(tag)}&level=${encodeURIComponent(level)}`
  const res = await fetch(`${baseUrl}/api/log/level`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body
  })
  if (!res.ok) throw new Error(`set log level failed: ${res.status}`)
}

// OTA check — returns 202 while in progress; result on 200.
export type OtaOutcome = 'up_to_date' | 'available' | 'no_asset' | 'check_failed' | 'unknown'

export interface OtaCheckResult {
  outcome: OtaOutcome
  update_available: boolean
  latest_version: string
  current_version: string
}

export interface OtaStatus {
  state: string
  in_progress: boolean
  progress_pct: number
}

interface UpdateStatus {
  current: string
  latest: string
  download_url: string
  available: boolean
  last_check_ok: boolean
  enabled: boolean
  last_check_ts?: number
  outcome?: OtaOutcome
}

// Kick a fresh manifest fetch on the device. Returns the last_check_ts
// observed BEFORE the kick so callers can wait for it to advance.
export async function kickOtaCheck(): Promise<number> {
  const before = await fetch(`${baseUrl}/api/update/status`)
  const beforeJson: UpdateStatus | null = before.ok ? await before.json() : null
  const beforeTs = beforeJson?.last_check_ts ?? 0

  const kick = await fetch(`${baseUrl}/api/update/check`, { method: 'POST' })
  if (!kick.ok && kick.status !== 202) {
    throw new Error(`ota check failed: ${kick.status}`)
  }
  return beforeTs
}

// Poll /api/update/status; returns the parsed result once last_check_ts
// advances past `since` OR outcome is a terminal non-unknown value, otherwise 'pending'.
export async function fetchOtaCheck(since = 0): Promise<OtaCheckResult | 'pending'> {
  const status = await fetch(`${baseUrl}/api/update/status`)
  if (!status.ok) throw new Error(`ota status failed: ${status.status}`)
  const s: UpdateStatus = await status.json()
  const tsAdvanced = (s.last_check_ts ?? 0) > since
  // Explicit outcome field from breadboard v0.42.0+
  if (s.outcome) {
    if (s.outcome === 'unknown') return 'pending'
    // Any other terminal outcome resolves immediately (ts may or may not have advanced)
  } else {
    // Legacy firmware: no outcome field. Pending until ts advances.
    if (!tsAdvanced) return 'pending'
  }
  const outcome: OtaOutcome = s.outcome ?? (s.last_check_ok ? (s.available ? 'available' : 'up_to_date') : 'check_failed')
  return {
    outcome,
    update_available: s.available,
    latest_version: s.latest,
    current_version: s.current,
  }
}

export async function triggerOtaUpdate(): Promise<void> {
  const res = await fetch(`${baseUrl}/api/update/apply`, { method: 'POST' })
  if (!res.ok) throw new Error(`ota update failed: ${res.status}`)
}

export async function fetchOtaStatus(): Promise<OtaStatus> {
  const res = await fetch(`${baseUrl}/api/update/progress`)
  if (!res.ok) throw new Error(`ota status failed: ${res.status}`)
  return res.json()
}

// Mark OTA as valid — POST /api/update/mark-valid.
export async function markOtaValid(): Promise<void> {
  const res = await fetch(`${baseUrl}/api/update/mark-valid`, { method: 'POST' })
  if (!res.ok) throw new Error(`mark-valid failed: ${res.status}`)
}

// Upload firmware binary to /api/update/push with progress callback.
export function uploadOta(
  file: File,
  onProgress: (pct: number) => void
): Promise<string> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest()
    xhr.open('POST', `${baseUrl}/api/update/push`)
    xhr.setRequestHeader('Content-Type', 'application/octet-stream')

    xhr.upload.addEventListener('progress', (e) => {
      if (e.lengthComputable) onProgress((e.loaded / e.total) * 100)
    })

    xhr.addEventListener('load', () => {
      if (xhr.status >= 200 && xhr.status < 300) {
        resolve(xhr.responseText || 'ok')
      } else {
        reject(new Error(`upload failed: ${xhr.status} ${xhr.responseText}`))
      }
    })

    xhr.addEventListener('error', () => reject(new Error('network error during upload')))
    xhr.addEventListener('abort', () => reject(new Error('upload aborted')))

    xhr.send(file)
  })
}
