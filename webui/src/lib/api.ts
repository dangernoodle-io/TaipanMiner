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

export interface Info {
  board: string
  version: string
  idf_version: string
  cores: number
  mac: string
  ssid: string | null
  flash_size: number
  app_size: number
  reset_reason: string | null
  wdt_resets: number | null
  boot_time: number | null
  worker_name: string
  validated: boolean
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

const baseUrl = import.meta.env.VITE_MINER_URL ?? ''

async function getJson<T>(path: string): Promise<T> {
  const res = await fetch(`${baseUrl}${path}`)
  if (!res.ok) throw new Error(`${path} failed: ${res.status}`)
  return res.json() as Promise<T>
}

export const fetchStats = () => getJson<Stats>('/api/stats')
export const fetchInfo  = () => getJson<Info>('/api/info')
export const fetchPower = () => getJson<Power>('/api/power')
export const fetchFan   = () => getJson<Fan>('/api/fan')
