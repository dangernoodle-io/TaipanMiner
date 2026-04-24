export interface Chip {
  idx: number
  total_ghs: number
  error_ghs: number
  hw_err_pct: number
  total_raw: number
  error_raw: number
  domain_ghs: number[]
}

export interface Stats {
  board: string
  version: string
  build_date: string
  build_time: string
  pool_host: string
  pool_port: number
  worker: string
  wallet: string
  pool_difficulty: number
  session_shares: number
  session_rejected: number
  lifetime_shares: number
  last_share_ago_s: number | null
  best_diff: number
  uptime_s: number
  rssi_dbm: number | null
  free_heap: number | null
  total_heap: number | null
  temp_c: number
  hashrate: number
  hashrate_avg: number
  hw_hashrate: number
  hw_shares: number
  asic_hashrate: number | null
  asic_hashrate_avg: number | null
  asic_shares: number | null
  asic_temp_c: number | null
  asic_freq_configured_mhz: number | null
  asic_freq_effective_mhz: number | null
  asic_small_cores: number | null
  asic_count: number | null
  asic_total_ghs: number | null
  asic_hw_error_pct: number | null
  asic_total_ghs_1m: number | null
  asic_total_ghs_10m: number | null
  asic_total_ghs_1h: number | null
  asic_hw_error_pct_1m: number | null
  asic_hw_error_pct_10m: number | null
  asic_hw_error_pct_1h: number | null
  asic_chips?: Chip[]
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
  validated: boolean
  network?: InfoNetwork
}

export interface Power {
  vcore_mv: number | null
  icore_ma: number | null
  pcore_mw: number | null
  efficiency_jth: number | null
  vin_mv: number | null
  board_temp_c: number | null
  vr_temp_c: number | null
}

export interface Fan {
  rpm: number | null
  duty_pct: number | null
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

export interface Settings {
  pool_host: string
  pool_port: number
  wallet: string
  worker: string
  pool_pass: string
  display_en?: boolean
  ota_skip_check?: boolean
}

export const fetchStats = () => getJson<Stats>('/api/stats')
export const fetchInfo  = () => getJson<Info>('/api/info')
export const fetchPower = () => getJson<Power>('/api/power')
export const fetchFan   = () => getJson<Fan>('/api/fan')
export const fetchSettings = () => getJson<Settings>('/api/settings')

export async function patchSettings(patch: Partial<Settings>): Promise<{ status: string; reboot_required: boolean }> {
  const res = await fetch(`${baseUrl}/api/settings`, {
    method: 'PATCH',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(patch)
  })
  if (!res.ok) throw new Error(`settings patch failed: ${res.status}`)
  return res.json()
}

export async function postReboot(): Promise<void> {
  const res = await fetch(`${baseUrl}/api/reboot`, { method: 'POST' })
  if (!res.ok) throw new Error(`reboot failed: ${res.status}`)
}

// OTA check — returns 202 while in progress; result on 200.
export interface OtaCheckResult {
  update_available: boolean
  latest_version: string
  current_version: string
}

export interface OtaStatus {
  state: string
  in_progress: boolean
  progress_pct: number
}

export async function fetchOtaCheck(): Promise<OtaCheckResult | 'pending'> {
  const res = await fetch(`${baseUrl}/api/ota/check`)
  if (res.status === 202) return 'pending'
  if (!res.ok) throw new Error(`ota check failed: ${res.status}`)
  return res.json()
}

export async function triggerOtaUpdate(): Promise<void> {
  const res = await fetch(`${baseUrl}/api/ota/update`, { method: 'POST' })
  if (!res.ok) throw new Error(`ota update failed: ${res.status}`)
}

export async function fetchOtaStatus(): Promise<OtaStatus> {
  const res = await fetch(`${baseUrl}/api/ota/status`)
  if (!res.ok) throw new Error(`ota status failed: ${res.status}`)
  return res.json()
}

// Upload firmware binary to /api/ota/push with progress callback.
export function uploadOta(
  file: File,
  onProgress: (pct: number) => void
): Promise<string> {
  return new Promise((resolve, reject) => {
    const xhr = new XMLHttpRequest()
    xhr.open('POST', `${baseUrl}/api/ota/push`)
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
