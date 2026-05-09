import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import {
  patchFan,
  fetchPool,
  fetchDiagAsic,
  fetchStats,
  fetchInfo,
  fetchHealth,
  fetchPower,
  fetchFan,
  fetchSettings,
  fetchKnot,
  patchSettings,
  putPool,
  switchPool,
  deletePoolSlot,
  postReboot,
  ping,
  fetchLogLevels,
  setLogLevel,
  fetchOtaCheck,
  triggerOtaUpdate,
  fetchOtaStatus,
} from './api'

// ---------------------------------------------------------------------------
// Shared fetch mock helper
// ---------------------------------------------------------------------------

function mockFetch(status: number, body: unknown = {}): ReturnType<typeof vi.fn> {
  const bodyStr = typeof body === 'string' ? body : JSON.stringify(body)
  return vi.fn(async () => new Response(bodyStr, { status })) as ReturnType<typeof vi.fn>
}

function setFetch(status: number, body: unknown = {}): ReturnType<typeof vi.fn> {
  const spy = mockFetch(status, body)
  ;(globalThis as unknown as { fetch: typeof fetch }).fetch = spy as unknown as typeof fetch
  return spy
}

afterEach(() => {
  vi.restoreAllMocks()
})

// ---------------------------------------------------------------------------
// patchFan (existing tests, preserved)
// ---------------------------------------------------------------------------

describe('patchFan', () => {
  let fetchSpy: ReturnType<typeof vi.fn>

  beforeEach(() => {
    fetchSpy = vi.fn(async () => new Response('', { status: 200 }))
    ;(globalThis as unknown as { fetch: typeof fetch }).fetch = fetchSpy as unknown as typeof fetch
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('POSTs form-urlencoded to /api/fan', async () => {
    await patchFan({ die_target_c: 65 })
    expect(fetchSpy).toHaveBeenCalledTimes(1)
    const [url, init] = fetchSpy.mock.calls[0]
    expect(url).toBe('/api/fan')
    expect(init.method).toBe('POST')
    expect(init.headers['Content-Type']).toBe('application/x-www-form-urlencoded')
    expect(init.body).toBe('die_target_c=65')
  })

  // TA-351 fixed: server-side parsing now accepts true/false/yes/no/on/off.
  it('encodes autofan as true / false', async () => {
    await patchFan({ autofan: true })
    expect(fetchSpy.mock.calls[0][1].body).toBe('autofan=true')

    await patchFan({ autofan: false })
    expect(fetchSpy.mock.calls[1][1].body).toBe('autofan=false')
  })

  it('serializes multiple fields and skips undefined', async () => {
    await patchFan({ autofan: true, die_target_c: 65, vr_target_c: 80, min_pct: 40, manual_pct: undefined })
    const body = fetchSpy.mock.calls[0][1].body as string
    const params = new URLSearchParams(body)
    expect(params.get('autofan')).toBe('true')
    expect(params.get('die_target_c')).toBe('65')
    expect(params.get('vr_target_c')).toBe('80')
    expect(params.get('min_pct')).toBe('40')
    expect(params.has('manual_pct')).toBe(false)
  })

  it('throws on non-OK response', async () => {
    fetchSpy.mockResolvedValueOnce(new Response('', { status: 400 }))
    await expect(patchFan({ die_target_c: 65 })).rejects.toThrow(/400/)
  })

  it('sends both die and vr targets', async () => {
    await patchFan({ die_target_c: 65, vr_target_c: 80 })
    const body = fetchSpy.mock.calls[0][1].body as string
    const params = new URLSearchParams(body)
    expect(params.get('die_target_c')).toBe('65')
    expect(params.get('vr_target_c')).toBe('80')
  })
})

// ---------------------------------------------------------------------------
// getJson-backed GET endpoints
// ---------------------------------------------------------------------------

describe('fetchStats', () => {
  it('GETs /api/stats and returns parsed JSON', async () => {
    const spy = setFetch(200, { uptime_s: 123 })
    const result = await fetchStats()
    expect(spy.mock.calls[0][0]).toBe('/api/stats')
    expect(result).toMatchObject({ uptime_s: 123 })
  })

  it('throws on non-OK response', async () => {
    setFetch(500)
    await expect(fetchStats()).rejects.toThrow('/api/stats')
  })
})

describe('fetchInfo', () => {
  it('GETs /api/info', async () => {
    const spy = setFetch(200, { board: 'bitaxe-601' })
    const result = await fetchInfo()
    expect(spy.mock.calls[0][0]).toBe('/api/info')
    expect(result).toMatchObject({ board: 'bitaxe-601' })
  })

  it('throws on error', async () => {
    setFetch(404)
    await expect(fetchInfo()).rejects.toThrow('/api/info')
  })
})

describe('fetchHealth', () => {
  it('GETs /api/health', async () => {
    const spy = setFetch(200, { ok: true })
    const result = await fetchHealth()
    expect(spy.mock.calls[0][0]).toBe('/api/health')
    expect(result).toMatchObject({ ok: true })
  })

  it('throws on error', async () => {
    setFetch(503)
    await expect(fetchHealth()).rejects.toThrow('/api/health')
  })
})

describe('fetchPower', () => {
  it('GETs /api/power', async () => {
    const spy = setFetch(200, { vcore_mv: 1200 })
    const result = await fetchPower()
    expect(spy.mock.calls[0][0]).toBe('/api/power')
    expect(result).toMatchObject({ vcore_mv: 1200 })
  })

  it('throws on error', async () => {
    setFetch(405)
    await expect(fetchPower()).rejects.toThrow('/api/power')
  })
})

describe('fetchFan', () => {
  it('GETs /api/fan', async () => {
    const spy = setFetch(200, { rpm: 2400, duty_pct: 50, autofan: true })
    const result = await fetchFan()
    expect(spy.mock.calls[0][0]).toBe('/api/fan')
    expect(result).toMatchObject({ rpm: 2400 })
  })

  it('throws on error', async () => {
    setFetch(405)
    await expect(fetchFan()).rejects.toThrow('/api/fan')
  })
})

describe('fetchSettings', () => {
  it('GETs /api/settings', async () => {
    const spy = setFetch(200, { hostname: 'taipan' })
    const result = await fetchSettings()
    expect(spy.mock.calls[0][0]).toBe('/api/settings')
    expect(result).toMatchObject({ hostname: 'taipan' })
  })

  it('throws on error', async () => {
    setFetch(500)
    await expect(fetchSettings()).rejects.toThrow('/api/settings')
  })
})

describe('fetchPool', () => {
  it('GETs /api/pool', async () => {
    const spy = setFetch(200, { connected: true, current_difficulty: 512 })
    const result = await fetchPool()
    expect(spy.mock.calls[0][0]).toBe('/api/pool')
    expect(result).toMatchObject({ current_difficulty: 512 })
  })

  it('throws on error', async () => {
    setFetch(503)
    await expect(fetchPool()).rejects.toThrow('/api/pool')
  })
})

describe('fetchKnot', () => {
  it('GETs /api/knot', async () => {
    const spy = setFetch(200, [{ instance: 'node1' }])
    const result = await fetchKnot()
    expect(spy.mock.calls[0][0]).toBe('/api/knot')
    expect(result).toHaveLength(1)
  })

  it('throws on error', async () => {
    setFetch(404)
    await expect(fetchKnot()).rejects.toThrow('/api/knot')
  })
})

describe('fetchDiagAsic', () => {
  it('GETs /api/diag/asic', async () => {
    const spy = setFetch(200, { recent_drops: [] })
    const result = await fetchDiagAsic()
    expect(spy.mock.calls[0][0]).toBe('/api/diag/asic')
    expect(result).toMatchObject({ recent_drops: [] })
  })

  it('throws on error', async () => {
    setFetch(500)
    await expect(fetchDiagAsic()).rejects.toThrow('/api/diag/asic')
  })
})

describe('fetchLogLevels', () => {
  it('GETs /api/log/level', async () => {
    const spy = setFetch(200, { levels: ['info'], tags: [] })
    const result = await fetchLogLevels()
    expect(spy.mock.calls[0][0]).toBe('/api/log/level')
    expect(result).toMatchObject({ levels: ['info'] })
  })

  it('throws on error', async () => {
    setFetch(500)
    await expect(fetchLogLevels()).rejects.toThrow('/api/log/level')
  })
})

// ---------------------------------------------------------------------------
// Non-trivial endpoints
// ---------------------------------------------------------------------------

describe('patchSettings', () => {
  it('PATCHes /api/settings with JSON body', async () => {
    const spy = setFetch(200, { status: 'ok', reboot_required: false })
    const result = await patchSettings({ hostname: 'newname' })
    const [url, init] = spy.mock.calls[0]
    expect(url).toBe('/api/settings')
    expect(init.method).toBe('PATCH')
    expect(init.headers['Content-Type']).toBe('application/json')
    expect(JSON.parse(init.body)).toEqual({ hostname: 'newname' })
    expect(result).toMatchObject({ status: 'ok' })
  })

  it('throws on non-OK response', async () => {
    setFetch(400)
    await expect(patchSettings({ hostname: 'bad' })).rejects.toThrow('settings patch')
  })
})

describe('putPool', () => {
  it('PUTs /api/pool with JSON body', async () => {
    const spy = setFetch(200)
    const body = {
      primary: { host: 'pool.example.com', port: 3333, worker: 'w', wallet: 'addr', pool_pass: 'x' },
      fallback: null,
    }
    await putPool(body)
    const [url, init] = spy.mock.calls[0]
    expect(url).toBe('/api/pool')
    expect(init.method).toBe('PUT')
    expect(JSON.parse(init.body)).toEqual(body)
  })

  it('throws on non-OK response', async () => {
    setFetch(400)
    await expect(putPool({ primary: { host: 'h', port: 1, worker: 'w', wallet: 'a', pool_pass: 'p' }, fallback: null })).rejects.toThrow('pool put')
  })
})

describe('switchPool', () => {
  it('POSTs /api/pool/switch with idx in body', async () => {
    const spy = setFetch(200)
    await switchPool(1)
    const [url, init] = spy.mock.calls[0]
    expect(url).toBe('/api/pool/switch')
    expect(init.method).toBe('POST')
    expect(JSON.parse(init.body)).toEqual({ idx: 1 })
  })

  it('throws on non-OK response', async () => {
    setFetch(409)
    await expect(switchPool(0)).rejects.toThrow('switch pool')
  })
})

describe('deletePoolSlot', () => {
  it('DELETEs /api/pool/fallback', async () => {
    const spy = setFetch(200)
    await deletePoolSlot('fallback')
    const [url, init] = spy.mock.calls[0]
    expect(url).toBe('/api/pool/fallback')
    expect(init.method).toBe('DELETE')
  })

  it('DELETEs /api/pool/primary', async () => {
    const spy = setFetch(200)
    await deletePoolSlot('primary')
    expect(spy.mock.calls[0][0]).toBe('/api/pool/primary')
  })

  it('throws with server error body on failure', async () => {
    setFetch(409, 'no fallback configured')
    await expect(deletePoolSlot('primary')).rejects.toThrow('no fallback configured')
  })
})

describe('postReboot', () => {
  it('POSTs /api/reboot', async () => {
    const spy = setFetch(200)
    await postReboot()
    expect(spy.mock.calls[0][0]).toBe('/api/reboot')
    expect(spy.mock.calls[0][1].method).toBe('POST')
  })

  it('throws on non-OK response', async () => {
    setFetch(503)
    await expect(postReboot()).rejects.toThrow('reboot failed')
  })
})

describe('setLogLevel', () => {
  it('POSTs form-urlencoded to /api/log/level', async () => {
    const spy = setFetch(200)
    await setLogLevel('wifi', 'debug')
    const [url, init] = spy.mock.calls[0]
    expect(url).toBe('/api/log/level')
    expect(init.method).toBe('POST')
    const params = new URLSearchParams(init.body)
    expect(params.get('tag')).toBe('wifi')
    expect(params.get('level')).toBe('debug')
  })

  it('throws on non-OK response', async () => {
    setFetch(400)
    await expect(setLogLevel('wifi', 'debug')).rejects.toThrow('set log level')
  })
})

// ---------------------------------------------------------------------------
// OTA endpoints
// ---------------------------------------------------------------------------

describe('fetchOtaCheck', () => {
  it('returns "pending" on 202', async () => {
    setFetch(202)
    const result = await fetchOtaCheck()
    expect(result).toBe('pending')
  })

  it('returns parsed result on 200', async () => {
    setFetch(200, { update_available: true, latest_version: '1.2.0', current_version: '1.1.0' })
    const result = await fetchOtaCheck()
    expect(result).toMatchObject({ update_available: true })
  })

  it('throws on non-OK non-202 response', async () => {
    setFetch(500)
    await expect(fetchOtaCheck()).rejects.toThrow('ota check')
  })
})

describe('triggerOtaUpdate', () => {
  it('POSTs /api/ota/update', async () => {
    const spy = setFetch(200)
    await triggerOtaUpdate()
    expect(spy.mock.calls[0][0]).toBe('/api/ota/update')
    expect(spy.mock.calls[0][1].method).toBe('POST')
  })

  it('throws on non-OK response', async () => {
    setFetch(503)
    await expect(triggerOtaUpdate()).rejects.toThrow('ota update')
  })
})

describe('fetchOtaStatus', () => {
  it('GETs /api/ota/status', async () => {
    const spy = setFetch(200, { state: 'idle', in_progress: false, progress_pct: 0 })
    const result = await fetchOtaStatus()
    expect(spy.mock.calls[0][0]).toBe('/api/ota/status')
    expect(result).toMatchObject({ state: 'idle' })
  })

  it('throws on non-OK response', async () => {
    setFetch(500)
    await expect(fetchOtaStatus()).rejects.toThrow('ota status')
  })
})

// ---------------------------------------------------------------------------
// ping
// ---------------------------------------------------------------------------

describe('ping', () => {
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('returns true when /api/version responds 200', async () => {
    setFetch(200, 'v1.0.0')
    const result = await ping(5000)
    expect(result).toBe(true)
  })

  it('returns false when response is non-OK', async () => {
    setFetch(503)
    const result = await ping(5000)
    expect(result).toBe(false)
  })

  it('returns false when fetch throws (network error)', async () => {
    ;(globalThis as unknown as { fetch: typeof fetch }).fetch = vi.fn(async () => {
      throw new Error('network error')
    }) as unknown as typeof fetch
    const result = await ping(5000)
    expect(result).toBe(false)
  })

  it('returns false when fetch is aborted (AbortError)', async () => {
    ;(globalThis as unknown as { fetch: typeof fetch }).fetch = vi.fn(async () => {
      throw new DOMException('The user aborted a request.', 'AbortError')
    }) as unknown as typeof fetch
    const result = await ping(5000)
    expect(result).toBe(false)
  })
})
