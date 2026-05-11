/**
 * Canonical /api/* response fixtures + a single `mockMinerApi` helper that
 * wires them onto a Playwright Page via `page.route()`.
 *
 * Per-test overrides: pass an `overrides` map keyed by endpoint path. The
 * override replaces the default body for that endpoint only.
 *
 * The fixtures represent a healthy bitaxe-601 miner — connected, hashing,
 * pool authorized, fan in autofan mode. Tests that need a different state
 * (disconnected, OTA available, no ASIC, etc.) override the relevant fields.
 */

import type { Page, Route } from '@playwright/test'

export const statsFixture = {
  session_shares: 42,
  session_rejected: 1,
  lifetime_shares: 1234,
  last_share_ago_s: 12,
  best_diff: 524288,
  uptime_s: 7200,
  temp_c: 40,
  hashrate: 485e9,
  hashrate_avg: 480e9,
  hashrate_1m: null,
  hashrate_10m: null,
  hashrate_1h: null,
  hw_error_pct_1m: null,
  hw_error_pct_10m: null,
  hw_error_pct_1h: null,
  shares: null,
  asic_hashrate: null,
  asic_hashrate_avg: 480e9,
  asic_shares: null,
  asic_temp_c: 68.5,
  asic_freq_configured_mhz: 525,
  asic_freq_effective_mhz: 524,
  asic_small_cores: 894,
  asic_count: 1,
  expected_ghs: 485,
  asic_total_ghs: 485.5,
  asic_hw_error_pct: 0.02,
  asic_total_ghs_1m: 484,
  asic_total_ghs_10m: 486,
  asic_total_ghs_1h: 483,
  asic_hw_error_pct_1m: 0.01,
  asic_hw_error_pct_10m: 0.02,
  asic_hw_error_pct_1h: 0.03,
  pool_effective_hashrate: 482e9,
  asic_chips: [
    {
      idx: 0,
      total_ghs: 485.5,
      error_ghs: 0.1,
      hw_err_pct: 0.02,
      total_raw: 1000000,
      error_raw: 200,
      domain_ghs: [121, 122, 121, 121],
      total_drops: 0,
      error_drops: 0,
      domain_drops: [0, 0, 0, 0],
      last_drop_ago_s: null,
    },
  ],
  rejected: {
    total: 1,
    job_not_found: 1,
    low_difficulty: 0,
    duplicate: 0,
    stale_prevhash: 0,
    other: 0,
    other_last_code: 0,
  },
}

export const infoFixture = {
  board: 'bitaxe-601',
  project_name: 'TaipanMiner',
  version: '1.2.3',
  idf_version: '5.5.3',
  build_date: 'Jan  1 2026',
  build_time: '12:00:00',
  chip_model: 'ESP32-S3',
  cores: 2,
  mac: 'AA:BB:CC:DD:EE:FF',
  ssid: 'example-wifi',
  flash_size: 8388608,
  app_size: 2097152,
  total_heap: 327680,
  free_heap: 180000,
  reset_reason: 'Power on',
  wdt_resets: 0,
  boot_time: 1735689600,
  worker_name: 'miner-1',
  hostname: 'taipanminer-test',
  validated: true,
  network: {
    ssid: 'example-wifi',
    bssid: '11:22:33:44:55:66',
    rssi: -55,
    ip: '192.0.2.10',
    connected: true,
    disc_reason: 0,
    disc_age_s: 7200,
    retry_count: 0,
    mdns: true,
    stratum: true,
    stratum_reconnect_ms: 0,
    stratum_fail_count: 0,
  },
}

export const healthFixture = {
  ok: true,
  free_heap: 180000,
  validated: true,
  network: {
    connected: true,
    rssi: -55,
    disc_age_s: 7200,
    retry_count: 0,
    mdns: 'taipanminer-test.local',
    stratum: true,
    stratum_fail_count: 0,
  },
}

export const powerFixture = {
  vcore_mv: 1180,
  icore_ma: 14500,
  pcore_mw: 17110,
  efficiency_jth: 35.3,
  vin_mv: 5050,
  vin_low: false,
  board_temp_c: 38.2,
  vr_temp_c: 62.4,
}

export const fanFixture = {
  rpm: 4200,
  duty_pct: 75,
  autofan: true,
  die_target_c: 65,
  vr_target_c: 80,
  manual_pct: 80,
  min_pct: 35,
  die_ema_c: 68.4,
  vr_ema_c: 62.1,
  pid_input_c: 68.4,
  pid_input_src: 'die' as const,
}

export const settingsFixture = {
  hostname: 'taipanminer-test',
  display_en: true,
  ota_skip_check: false,
  mdns_en: true,
  knot_en: true,
}

export const poolFixture = {
  host: 'pool.example.com',
  port: 3333,
  worker: 'miner-1',
  wallet: 'bc1qexamplewallet0000000000000000000000abcd',
  connected: true,
  session_start_ago_s: 7100,
  current_difficulty: 65536,
  pool_effective_hashrate: 482e9,
  pool_effective_hashrate_1m: 480e9,
  pool_effective_hashrate_10m: 481e9,
  pool_effective_hashrate_1h: 482e9,
  latency_ms: 35,
  extranonce1: 'abcdef01',
  extranonce2_size: 8,
  version_mask: '1fffe000',
  notify: null,
  active_pool_idx: 0 as const,
  extranonce_subscribe_status: 'active' as const,
  configured: {
    primary: {
      host: 'pool.example.com',
      port: 3333,
      worker: 'miner-1',
      wallet: 'bc1qexamplewallet0000000000000000000000abcd',
      extranonce_subscribe: true,
      decode_coinbase: true,
    },
    fallback: null,
  },
}

export const knotFixture: unknown[] = []

export const otaCheckFixture = {
  update_available: false,
  latest_version: '1.2.3',
  current_version: '1.2.3',
}

export const otaStatusFixture = {
  state: 'idle',
  in_progress: false,
  progress_pct: 0,
}

export const diagAsicFixture = {
  recent_drops: [],
}

export const logLevelsFixture = {
  levels: ['error', 'warn', 'info', 'debug', 'verbose', 'none'],
  tags: [
    { tag: '*', level: 'info' },
    { tag: 'wifi', level: 'warn' },
  ],
}

export type EndpointMap = Partial<{
  '/api/stats': unknown
  '/api/info': unknown
  '/api/health': unknown
  '/api/power': unknown
  '/api/fan': unknown
  '/api/settings': unknown
  '/api/pool': unknown
  '/api/knot': unknown
  '/api/ota/check': unknown
  '/api/ota/status': unknown
  '/api/diag/asic': unknown
  '/api/log/level': unknown
  '/api/version': unknown
}>

const defaults: EndpointMap = {
  '/api/stats': statsFixture,
  '/api/info': infoFixture,
  '/api/health': healthFixture,
  '/api/power': powerFixture,
  '/api/fan': fanFixture,
  '/api/settings': settingsFixture,
  '/api/pool': poolFixture,
  '/api/knot': knotFixture,
  '/api/ota/check': otaCheckFixture,
  '/api/ota/status': otaStatusFixture,
  '/api/diag/asic': diagAsicFixture,
  '/api/log/level': logLevelsFixture,
  '/api/version': '1.2.3',
}

export interface MockOptions {
  overrides?: EndpointMap
  /** Endpoints that should respond with the given status (no body). */
  statusOverrides?: Partial<Record<keyof EndpointMap | string, number>>
  /** Block these endpoints entirely (404). Useful to simulate tdongle (no /api/power). */
  notFound?: (keyof EndpointMap | string)[]
}

/**
 * Wires `page.route()` handlers for every /api/* endpoint the miner SPA polls.
 * Call once per test before navigation.
 */
export async function mockMinerApi(page: Page, opts: MockOptions = {}): Promise<void> {
  const merged: EndpointMap = { ...defaults, ...(opts.overrides ?? {}) }
  const notFound = new Set(opts.notFound ?? [])
  const statusOverrides = opts.statusOverrides ?? {}

  // Catch-all /api/** route — match by URL pathname.
  await page.route('**/api/**', async (route: Route) => {
    const url = new URL(route.request().url())
    const path = url.pathname

    if (notFound.has(path)) {
      await route.fulfill({ status: 404, body: 'not found' })
      return
    }

    if (path in statusOverrides) {
      await route.fulfill({ status: statusOverrides[path]!, body: '' })
      return
    }

    if (path in merged) {
      const body = merged[path as keyof EndpointMap]
      const isPlainText = path === '/api/version'
      await route.fulfill({
        status: 200,
        contentType: isPlainText ? 'text/plain' : 'application/json',
        body: isPlainText ? String(body) : JSON.stringify(body),
      })
      return
    }

    // Unknown endpoint — return 404 so the test surfaces the gap.
    await route.fulfill({ status: 404, body: `unmocked: ${path}` })
  })
}
