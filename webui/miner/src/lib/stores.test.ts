import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { startRebootRecovery, rebooting, __resetRebootPoll, start, stop, stats, info, settings, power, fan, thermal, pool, health, hasAsic, connected, history } from './stores'
import { get } from 'svelte/store'

vi.mock('./api', () => ({
  ping: vi.fn(),
  fetchStats: vi.fn(),
  fetchInfo: vi.fn(),
  fetchHealth: vi.fn(),
  fetchSensors: vi.fn(),
  fetchSettings: vi.fn(),
  fetchPool: vi.fn(),
}))

import { ping as apiPing, fetchStats, fetchInfo, fetchHealth, fetchSensors, fetchSettings, fetchPool } from './api'

// ---------------------------------------------------------------------------
// Shared poll mock setup
// ---------------------------------------------------------------------------

const STUB_STATS = {
  uptime_s: 100,
  session_shares: 0,
  session_rejected: 0,
  lifetime: { shares: 0, best_diff: 0 },
  last_share_ago_s: null,
  best_diff: 0,
  temp_c: 45,
  hashrate: 0,
  hashrate_avg: 0,
  shares: null,
  asic_hashrate: null,
  asic_hashrate_avg: null,
  asic_shares: null,
  asic_freq_configured_mhz: null,
  asic_freq_effective_mhz: null,
  asic_small_cores: null,
  asic_count: null,
  expected_ghs: null,
  asic_total_ghs: null,
  asic_hw_error_pct: null,
  asic_total_ghs_1m: null,
  asic_total_ghs_10m: null,
  asic_total_ghs_1h: null,
  asic_hw_error_pct_1m: null,
  asic_hw_error_pct_10m: null,
  asic_hw_error_pct_1h: null,
  hashrate_1m: null,
  hashrate_10m: null,
  hashrate_1h: null,
  hw_error_pct_1m: null,
  hw_error_pct_10m: null,
  hw_error_pct_1h: null,
  pool_effective_hashrate: null,
}

// eslint-disable-next-line @typescript-eslint/no-explicit-any
type AnyFn = (...args: any[]) => any

const STUB_SENSORS = {
  fan: { rpm: 4200, duty_pct: 75, autofan: true, die_target_c: 65, vr_target_c: 80, manual_pct: 80, min_pct: 35, die_ema_c: null, vr_ema_c: null, pid_input_c: null, pid_input_src: 'die' as const },
  power: { present: true, vin_mv: 5050, pout_mw: 17000 },
  thermal: {
    soc: { present: true, c: 42 },
    vr: { present: true, c: 62 },
    asic: { present: true, c: 68 },
    board: { present: true, c: 38 },
  },
  miner: { vcore_mv: 1180, icore_ma: 14500, pcore_mw: 17110, vr_temp_c: 62.4, efficiency_jth: 35.3, efficiency_jth_1m: null, efficiency_jth_10m: null, efficiency_jth_1h: null, expected_efficiency_jth: null, vin_low: false },
}

function setupPollMocks(overrides: {
  stats?: Partial<typeof STUB_STATS> | AnyFn
  sensors?: typeof STUB_SENSORS | null
  pool?: object | null
  health?: object | null
  info?: object | null
  settings?: object | null
} = {}) {
  if (typeof overrides.stats === 'function') {
    vi.mocked(fetchStats).mockImplementation(overrides.stats as AnyFn)
  } else {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    vi.mocked(fetchStats).mockResolvedValue({ ...STUB_STATS, ...(overrides.stats ?? {}) } as any)
  }

  const sensorsVal = overrides.sensors === undefined ? STUB_SENSORS : overrides.sensors
  if (sensorsVal === null) {
    vi.mocked(fetchSensors).mockRejectedValue(new Error('no sensors'))
  } else {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    vi.mocked(fetchSensors).mockResolvedValue(sensorsVal as any)
  }

  const poolVal = overrides.pool === undefined ? { connected: true, current_difficulty: 512 } : overrides.pool
  if (poolVal === null) {
    vi.mocked(fetchPool).mockRejectedValue(new Error('no pool'))
  } else {
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    vi.mocked(fetchPool).mockResolvedValue(poolVal as any)
  }

  vi.mocked(fetchHealth).mockResolvedValue(
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    (overrides.health ?? { ok: true, free_heap: 100000, validated: true, network: { connected: true, rssi: -60, disc_age_s: 0, retry_count: 0, mdns: null } }) as any
  )
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  vi.mocked(fetchInfo).mockResolvedValue((overrides.info ?? { board: 'test', capabilities: [] }) as any)
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  vi.mocked(fetchSettings).mockResolvedValue((overrides.settings ?? { hostname: 'taipan' }) as any)
}

// ---------------------------------------------------------------------------
// Setup / teardown
// ---------------------------------------------------------------------------

beforeEach(() => {
  vi.useFakeTimers()
  vi.clearAllMocks()
  vi.clearAllTimers()
  __resetRebootPoll()
  rebooting.set({ active: false, reason: '', elapsed: 0, timedOut: false })
  stop()
  // reset stores to initial state
  stats.set(null)
  info.set(null)
  health.set(null)
  settings.set(null)
  power.set(null)
  fan.set(null)
  thermal.set(null)
  pool.set(null)
  hasAsic.set(false)
  connected.set(false)
  history.set([])
})

afterEach(() => {
  stop()
  vi.useRealTimers()
})

// ---------------------------------------------------------------------------
// startRebootRecovery
// ---------------------------------------------------------------------------

describe('startRebootRecovery', () => {
  it('sets rebooting.active to true with reason', () => {
    startRebootRecovery('test-reboot')
    const state = get(rebooting)
    expect(state.active).toBe(true)
    expect(state.reason).toBe('test-reboot')
  })

  it('initializes elapsed and timedOut correctly', () => {
    startRebootRecovery('flash')
    const state = get(rebooting)
    expect(state.elapsed).toBe(0)
    expect(state.timedOut).toBe(false)
  })

  it('sets correct initial state structure', () => {
    startRebootRecovery('update')
    const state = get(rebooting)
    expect(state).toHaveProperty('active')
    expect(state).toHaveProperty('reason')
    expect(state).toHaveProperty('elapsed')
    expect(state).toHaveProperty('timedOut')
  })
})

// ---------------------------------------------------------------------------
// reboot recovery polling lifecycle
// ---------------------------------------------------------------------------

describe('reboot recovery polling lifecycle', () => {
  it('does not poll during the initial 3s pre-wait', async () => {
    vi.mocked(apiPing).mockResolvedValue(false)
    startRebootRecovery('test')
    await vi.advanceTimersByTimeAsync(2999)
    expect(apiPing).not.toHaveBeenCalled()
  })

  it('starts polling 1s after the pre-wait', async () => {
    vi.mocked(apiPing).mockResolvedValue(false)
    startRebootRecovery('test')
    await vi.advanceTimersByTimeAsync(3000)
    await vi.advanceTimersByTimeAsync(1000)
    expect(apiPing).toHaveBeenCalledTimes(1)
    expect(apiPing).toHaveBeenCalledWith(1500)
  })

  it('clears reboot state when ping returns true', async () => {
    vi.mocked(apiPing).mockResolvedValue(true)
    startRebootRecovery('flash')
    await vi.advanceTimersByTimeAsync(3000)
    await vi.advanceTimersByTimeAsync(1000)
    const state = get(rebooting)
    expect(state.active).toBe(false)
  })

  it('stops polling once cleared (no more calls after success)', async () => {
    vi.mocked(apiPing).mockResolvedValue(true)
    startRebootRecovery('test')
    await vi.advanceTimersByTimeAsync(3000)
    await vi.advanceTimersByTimeAsync(1000)
    expect(apiPing).toHaveBeenCalledTimes(1)
    await vi.advanceTimersByTimeAsync(5000)
    expect(apiPing).toHaveBeenCalledTimes(1)
  })

  it('sets timedOut=true after 90s without success', async () => {
    vi.mocked(apiPing).mockResolvedValue(false)
    startRebootRecovery('test')
    await vi.advanceTimersByTimeAsync(3000)
    await vi.advanceTimersByTimeAsync(1000)
    await vi.advanceTimersByTimeAsync(91000)
    const state = get(rebooting)
    expect(state.timedOut).toBe(true)
    expect(state.elapsed).toBeGreaterThan(90)
  })

  it('double startRebootRecovery is a no-op while one is active', async () => {
    vi.mocked(apiPing).mockResolvedValue(false)
    startRebootRecovery('first')
    await vi.advanceTimersByTimeAsync(3000)
    // Now rebootPollId is set by the setTimeout callback
    startRebootRecovery('second')
    const state = get(rebooting)
    expect(state.reason).toBe('first')
  })

  it('elapsed counter increments while polling', async () => {
    vi.mocked(apiPing).mockResolvedValue(false)
    startRebootRecovery('test')
    await vi.advanceTimersByTimeAsync(3000)
    await vi.advanceTimersByTimeAsync(1000)
    let state = get(rebooting)
    expect(state.elapsed).toBeGreaterThanOrEqual(4)
    await vi.advanceTimersByTimeAsync(3000)
    state = get(rebooting)
    expect(state.elapsed).toBeGreaterThanOrEqual(7)
    expect(state.timedOut).toBe(false)
  })
})

// ---------------------------------------------------------------------------
// start() / stop()
// ---------------------------------------------------------------------------

describe('start()', () => {
  it('calls poll immediately on start', async () => {
    setupPollMocks()
    start()
    await vi.advanceTimersByTimeAsync(0)
    expect(fetchStats).toHaveBeenCalledTimes(1)
  })

  it('sets connected=true after successful poll', async () => {
    setupPollMocks()
    start()
    await vi.advanceTimersByTimeAsync(0)
    expect(get(connected)).toBe(true)
  })

  it('populates stats store after poll', async () => {
    setupPollMocks({ stats: { uptime_s: 42 } })
    start()
    await vi.advanceTimersByTimeAsync(0)
    expect(get(stats)).toMatchObject({ uptime_s: 42 })
  })

  it('polls again after 5s interval', async () => {
    setupPollMocks()
    start()
    await vi.advanceTimersByTimeAsync(0) // initial poll
    await vi.advanceTimersByTimeAsync(5000) // interval
    expect(fetchStats).toHaveBeenCalledTimes(2)
  })

  it('is idempotent — double start does not create extra intervals', async () => {
    setupPollMocks()
    start()
    start()
    await vi.advanceTimersByTimeAsync(0)
    await vi.advanceTimersByTimeAsync(5000)
    // If two intervals were active, fetchStats would be called 3+ times
    expect(fetchStats).toHaveBeenCalledTimes(2)
  })

  it('loads info and settings on first poll', async () => {
    setupPollMocks()
    start()
    await vi.advanceTimersByTimeAsync(0)
    expect(fetchInfo).toHaveBeenCalledTimes(1)
    expect(fetchSettings).toHaveBeenCalledTimes(1)
    expect(get(info)).toMatchObject({ board: 'test' })
    expect(get(settings)).toMatchObject({ hostname: 'taipan' })
  })

  it('does not reload info/settings on second poll (lazy-load)', async () => {
    setupPollMocks()
    start()
    await vi.advanceTimersByTimeAsync(0)
    await vi.advanceTimersByTimeAsync(5000)
    // After both polls, still only 1 call each (loaded on first poll)
    expect(fetchInfo).toHaveBeenCalledTimes(1)
    expect(fetchSettings).toHaveBeenCalledTimes(1)
  })

  it('sets connected=false after 2+ consecutive fetchStats failures', async () => {
    vi.mocked(fetchStats).mockRejectedValue(new Error('network error'))
    start()
    await vi.advanceTimersByTimeAsync(0)   // fail 1
    await vi.advanceTimersByTimeAsync(5000) // fail 2
    expect(get(connected)).toBe(false)
  })

  it('appends one history sample per poll', async () => {
    setupPollMocks()
    start()
    await vi.advanceTimersByTimeAsync(0)
    expect(get(history)).toHaveLength(1)
    await vi.advanceTimersByTimeAsync(5000)
    expect(get(history)).toHaveLength(2)
  })

  it('hasAsic=false when sensors.power.present=false and no info capabilities', async () => {
    setupPollMocks({
      // eslint-disable-next-line @typescript-eslint/no-explicit-any
      sensors: { ...STUB_SENSORS, power: { present: false, vin_mv: null } } as any,
      info: { board: 'tdongle-s3', capabilities: [] },
    })
    start()
    await vi.advanceTimersByTimeAsync(0)
    expect(get(hasAsic)).toBe(false)
  })

  it('hasAsic=true when info.capabilities includes asic', async () => {
    setupPollMocks({ info: { board: 'bitaxe-601', capabilities: ['asic', 'fan', 'power'] } })
    start()
    await vi.advanceTimersByTimeAsync(0)
    expect(get(hasAsic)).toBe(true)
  })

  it('hasAsic=false when info.capabilities does not include asic', async () => {
    setupPollMocks({ info: { board: 'tdongle-s3', capabilities: ['ui'] } })
    start()
    await vi.advanceTimersByTimeAsync(0)
    expect(get(hasAsic)).toBe(false)
  })

  it('fetches sensors every poll', async () => {
    setupPollMocks()
    start()
    await vi.advanceTimersByTimeAsync(0)
    expect(fetchSensors).toHaveBeenCalledTimes(1)
    await vi.advanceTimersByTimeAsync(5000)
    expect(fetchSensors).toHaveBeenCalledTimes(2)
  })

  it('power store reconstructs vcore_mv from miner and vin_mv from power', async () => {
    setupPollMocks()
    start()
    await vi.advanceTimersByTimeAsync(0)
    const p = get(power)
    expect(p?.vcore_mv).toBe(1180)          // from miner
    expect(p?.vin_mv).toBe(5050)            // from power
    expect(p?.present).toBe(true)           // from power.present
  })

  it('thermal store populated from sensors', async () => {
    setupPollMocks()
    start()
    await vi.advanceTimersByTimeAsync(0)
    const t = get(thermal)
    expect(t?.asic.present).toBe(true)
    expect(t?.asic.c).toBe(68)
  })

  it('fan store populated from sensors', async () => {
    setupPollMocks()
    start()
    await vi.advanceTimersByTimeAsync(0)
    const f = get(fan)
    expect(f?.rpm).toBe(4200)
    expect(f?.duty_pct).toBe(75)
  })
})

describe('stop()', () => {
  it('stops the polling interval', async () => {
    setupPollMocks()
    start()
    await vi.advanceTimersByTimeAsync(0)
    stop()
    await vi.advanceTimersByTimeAsync(5000)
    // No additional poll after stop
    expect(fetchStats).toHaveBeenCalledTimes(1)
  })

  it('is safe to call when not started', () => {
    expect(() => stop()).not.toThrow()
  })

  it('allows restart after stop', async () => {
    setupPollMocks()
    start()
    await vi.advanceTimersByTimeAsync(0)
    stop()
    start()
    await vi.advanceTimersByTimeAsync(0)
    expect(fetchStats).toHaveBeenCalledTimes(2)
  })

  it('reloads info and settings after restart (stop resets lazy-load flags)', async () => {
    setupPollMocks()
    start()
    await vi.advanceTimersByTimeAsync(0)
    expect(fetchInfo).toHaveBeenCalledTimes(1)
    stop()
    start()
    await vi.advanceTimersByTimeAsync(0)
    expect(fetchInfo).toHaveBeenCalledTimes(2)
  })
})

// ---------------------------------------------------------------------------
// poll() — reboot detection via uptime_s drop
// ---------------------------------------------------------------------------

describe('poll() reboot detection', () => {
  it('refetches info/settings when uptime_s drops', async () => {
    let callCount = 0
    vi.mocked(fetchStats).mockImplementation(async () => {
      callCount++
      return { ...STUB_STATS, uptime_s: callCount === 1 ? 100 : 5 } as Awaited<ReturnType<typeof fetchStats>>
    })
    vi.mocked(fetchSensors).mockRejectedValue(new Error('no sensors'))
    vi.mocked(fetchPool).mockResolvedValue({ connected: true } as Awaited<ReturnType<typeof fetchPool>>)
    vi.mocked(fetchHealth).mockResolvedValue({ ok: true } as Awaited<ReturnType<typeof fetchHealth>>)
    vi.mocked(fetchInfo).mockResolvedValue({ board: 'test', capabilities: [] } as unknown as Awaited<ReturnType<typeof fetchInfo>>)
    vi.mocked(fetchSettings).mockResolvedValue({ hostname: 'test' } as Awaited<ReturnType<typeof fetchSettings>>)

    start()
    await vi.advanceTimersByTimeAsync(0)   // poll 1: uptime=100, info loaded
    expect(fetchInfo).toHaveBeenCalledTimes(1)
    await vi.advanceTimersByTimeAsync(5000) // poll 2: uptime=5 → drop → reload
    expect(fetchInfo).toHaveBeenCalledTimes(2)
  })
})
