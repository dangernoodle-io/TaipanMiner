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
  fetchDiagHeap,
  checkDiagHeap,
  fetchDiagTasks,
  fetchDiagPanic,
  clearAbnormalResets,
  clearDiagPanic,
  coredumpUrl,
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

  it('PATCHes /api/settings with mdns_en field', async () => {
    const spy = setFetch(200, { status: 'ok', reboot_required: false })
    await patchSettings({ mdns_en: false })
    const [, init] = spy.mock.calls[0]
    expect(JSON.parse(init.body)).toEqual({ mdns_en: false })
  })

  it('PATCHes /api/settings with knot_en field', async () => {
    const spy = setFetch(200, { status: 'ok', reboot_required: false })
    await patchSettings({ knot_en: true })
    const [, init] = spy.mock.calls[0]
    expect(JSON.parse(init.body)).toEqual({ knot_en: true })
  })

  it('throws on non-OK response', async () => {
    setFetch(400)
    await expect(patchSettings({ hostname: 'bad' })).rejects.toThrow('settings patch')
  })
})

describe('fetchHealth — knot field', () => {
  it('returns health with network.knot when present', async () => {
    const spy = setFetch(200, { ok: true, free_heap: 100000, validated: true, network: { connected: true, rssi: -50, disc_age_s: 0, retry_count: 0, mdns: null, knot: true } })
    const result = await fetchHealth()
    expect(spy.mock.calls[0][0]).toBe('/api/health')
    expect(result.network.knot).toBe(true)
  })

  it('returns health with network.knot=false', async () => {
    const spy = setFetch(200, { ok: true, free_heap: 100000, validated: true, network: { connected: true, rssi: -50, disc_age_s: 0, retry_count: 0, mdns: null, knot: false } })
    const result = await fetchHealth()
    expect(result.network.knot).toBe(false)
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

// ---------------------------------------------------------------------------
// uploadOta
// ---------------------------------------------------------------------------

import { uploadOta } from './api'

/** Build a minimal XHR shim that calls the event listeners synchronously. */
function makeXhrShim(opts: {
  status: number
  responseText?: string
  failLoad?: boolean
  failError?: boolean
  failAbort?: boolean
}) {
  return class MockXHR {
    open = vi.fn()
    setRequestHeader = vi.fn()
    send = vi.fn(() => {
      // Fire upload progress first
      this.upload.dispatchEvent('progress', { lengthComputable: true, loaded: 50, total: 100 })

      if (opts.failError) {
        this.dispatchEvent('error', {})
        return
      }
      if (opts.failAbort) {
        this.dispatchEvent('abort', {})
        return
      }
      // Simulate load
      ;(this as any).status = opts.status
      ;(this as any).responseText = opts.responseText ?? ''
      this.dispatchEvent('load', {})
    })

    status = 0
    responseText = ''

    _listeners: Record<string, ((e: any) => void)[]> = {}
    upload = {
      _listeners: {} as Record<string, ((e: any) => void)[]>,
      addEventListener(type: string, cb: (e: any) => void) {
        if (!this._listeners[type]) this._listeners[type] = []
        this._listeners[type].push(cb)
      },
      dispatchEvent(type: string, e: any) {
        ;(this._listeners[type] ?? []).forEach(cb => cb(e))
      }
    }

    addEventListener(type: string, cb: (e: any) => void) {
      if (!this._listeners[type]) this._listeners[type] = []
      this._listeners[type].push(cb)
    }
    dispatchEvent(type: string, e: any) {
      ;(this._listeners[type] ?? []).forEach(cb => cb(e))
    }
  }
}

describe('uploadOta', () => {
  afterEach(() => {
    vi.restoreAllMocks()
  })

  it('resolves with responseText on 200', async () => {
    ;(globalThis as any).XMLHttpRequest = makeXhrShim({ status: 200, responseText: 'ok' })
    const file = new File(['data'], 'fw.bin')
    const onProgress = vi.fn()
    const result = await uploadOta(file, onProgress)
    expect(result).toBe('ok')
    expect(onProgress).toHaveBeenCalledWith(50)
  })

  it('rejects on non-2xx status', async () => {
    ;(globalThis as any).XMLHttpRequest = makeXhrShim({ status: 500, responseText: 'Internal Error' })
    const file = new File(['data'], 'fw.bin')
    await expect(uploadOta(file, vi.fn())).rejects.toThrow('upload failed: 500')
  })

  it('rejects on network error', async () => {
    ;(globalThis as any).XMLHttpRequest = makeXhrShim({ status: 0, failError: true })
    const file = new File(['data'], 'fw.bin')
    await expect(uploadOta(file, vi.fn())).rejects.toThrow('network error')
  })

  it('rejects on abort', async () => {
    ;(globalThis as any).XMLHttpRequest = makeXhrShim({ status: 0, failAbort: true })
    const file = new File(['data'], 'fw.bin')
    await expect(uploadOta(file, vi.fn())).rejects.toThrow('upload aborted')
  })
})

// ---------------------------------------------------------------------------
// Heap diagnostics
// ---------------------------------------------------------------------------

describe('fetchDiagHeap', () => {
  it('GETs /api/diag/heap and returns parsed JSON', async () => {
    const spy = setFetch(200, {
      internal: { free: 1000, allocated: 500, largest_free_block: 400, minimum_ever_free: 100 },
      dma: { free: 2000, allocated: 1000, largest_free_block: 1500, minimum_ever_free: 500 },
      default: { free: 3000, allocated: 2000, largest_free_block: 2500, minimum_ever_free: 1000 },
    })
    const result = await fetchDiagHeap()
    expect(spy.mock.calls[0][0]).toBe('/api/diag/heap')
    expect(result).toMatchObject({ internal: { free: 1000 } })
  })

  it('throws on non-OK response', async () => {
    setFetch(500)
    await expect(fetchDiagHeap()).rejects.toThrow('/api/diag/heap')
  })
})

describe('checkDiagHeap', () => {
  it('POSTs to /api/diag/heap/check and returns ok boolean', async () => {
    const spy = setFetch(200, { ok: true })
    const result = await checkDiagHeap()
    const [url, init] = spy.mock.calls[0]
    expect(url).toBe('/api/diag/heap/check')
    expect(init.method).toBe('POST')
    expect(result).toBe(true)
  })

  it('returns false when ok is false', async () => {
    setFetch(200, { ok: false })
    const result = await checkDiagHeap()
    expect(result).toBe(false)
  })

  it('throws on non-OK response', async () => {
    setFetch(500)
    await expect(checkDiagHeap()).rejects.toThrow('heap check failed')
  })
})

// ---------------------------------------------------------------------------
// Task diagnostics
// ---------------------------------------------------------------------------

describe('fetchDiagTasks', () => {
  it('GETs /api/diag/tasks and returns array', async () => {
    const spy = setFetch(200, [
      { name: 'IDLE1', prio: 0, base_prio: 0, stack_hwm: 100, state: 'ready' },
      { name: 'mining', prio: 20, base_prio: 20, stack_hwm: 500, state: 'running' },
    ])
    const result = await fetchDiagTasks()
    expect(spy.mock.calls[0][0]).toBe('/api/diag/tasks')
    expect(result).toHaveLength(2)
    expect(result[0].name).toBe('IDLE1')
  })

  it('throws on non-OK response', async () => {
    setFetch(500)
    await expect(fetchDiagTasks()).rejects.toThrow('/api/diag/tasks')
  })
})

// ---------------------------------------------------------------------------
// Panic diagnostics
// ---------------------------------------------------------------------------

describe('fetchDiagPanic', () => {
  it('GETs /api/diag/panic and returns panic data', async () => {
    const spy = setFetch(200, {
      available: true,
      coredump: true,
      boots_since: 2,
      task: 'mining',
      exc_pc: 0x400d1234,
      exc_cause: 28,
      panic_reason: 'Stack overflow',
    })
    const result = await fetchDiagPanic()
    expect(spy.mock.calls[0][0]).toBe('/api/diag/panic')
    expect(result).toMatchObject({ available: true, coredump: true })
  })

  it('throws on non-OK response', async () => {
    setFetch(500)
    await expect(fetchDiagPanic()).rejects.toThrow('/api/diag/panic')
  })
})

// ---------------------------------------------------------------------------
// Reset and panic clearing
// ---------------------------------------------------------------------------

describe('clearAbnormalResets', () => {
  it('DELETEs /api/diag/abnormal-resets', async () => {
    const spy = setFetch(200)
    await clearAbnormalResets()
    const [url, init] = spy.mock.calls[0]
    expect(url).toBe('/api/diag/abnormal-resets')
    expect(init.method).toBe('DELETE')
  })

  it('throws on non-OK response', async () => {
    setFetch(500)
    await expect(clearAbnormalResets()).rejects.toThrow('clear abnormal-resets failed')
  })
})

describe('clearDiagPanic', () => {
  it('DELETEs /api/diag/panic', async () => {
    const spy = setFetch(200)
    await clearDiagPanic()
    const [url, init] = spy.mock.calls[0]
    expect(url).toBe('/api/diag/panic')
    expect(init.method).toBe('DELETE')
  })

  it('throws on non-OK response', async () => {
    setFetch(500)
    await expect(clearDiagPanic()).rejects.toThrow('clear panic failed')
  })
})

// ---------------------------------------------------------------------------
// Coredump constant
// ---------------------------------------------------------------------------

describe('coredumpUrl', () => {
  it('exports const pointing to coredump endpoint', () => {
    expect(coredumpUrl).toContain('/api/diag/panic/coredump')
  })
})
