import { describe, it, expect, vi, beforeEach } from 'vitest'
import { fetchScan, fetchVersion, postSave, type SaveBody } from './api'

function mockFetch(status: number, body: unknown = {}): ReturnType<typeof vi.fn> {
  const bodyStr = typeof body === 'string' ? body : JSON.stringify(body)
  return vi.fn(async () => new Response(bodyStr, { status })) as ReturnType<typeof vi.fn>
}

function setFetch(status: number, body: unknown = {}): ReturnType<typeof vi.fn> {
  const spy = mockFetch(status, body)
  ;(globalThis as unknown as { fetch: typeof fetch }).fetch = spy as unknown as typeof fetch
  return spy
}

beforeEach(() => {
  vi.clearAllMocks()
})

describe('fetchScan', () => {
  it('GETs /api/scan and returns parsed JSON', async () => {
    const spy = setFetch(200, [{ ssid: 'test-network', rssi: -50, secure: true }])
    const result = await fetchScan()
    expect(spy.mock.calls[0][0]).toBe('/api/scan')
    expect(result).toHaveLength(1)
    expect(result[0]).toMatchObject({ ssid: 'test-network', rssi: -50, secure: true })
  })

  it('throws on non-OK response', async () => {
    setFetch(500)
    await expect(fetchScan()).rejects.toThrow('scan failed')
  })

  it('returns empty array on success with empty list', async () => {
    const spy = setFetch(200, [])
    const result = await fetchScan()
    expect(result).toEqual([])
  })
})

describe('fetchVersion', () => {
  it('GETs /api/version and returns trimmed text', async () => {
    const spy = setFetch(200, 'v1.2.3')
    const result = await fetchVersion()
    expect(spy.mock.calls[0][0]).toBe('/api/version')
    expect(result).toBe('v1.2.3')
  })

  it('trims whitespace from response', async () => {
    const spy = vi.fn(async () => new Response('  v1.2.3  \n', { status: 200 }))
    ;(globalThis as unknown as { fetch: typeof fetch }).fetch = spy as unknown as typeof fetch
    const result = await fetchVersion()
    expect(result).toBe('v1.2.3')
  })

  it('throws on non-OK response', async () => {
    setFetch(404)
    await expect(fetchVersion()).rejects.toThrow('version failed')
  })
})

describe('postSave', () => {
  it('POSTs form-urlencoded to /save', async () => {
    const spy = setFetch(200)
    const body: SaveBody = {
      ssid: 'test-ssid',
      pass: 'password123',
      hostname: 'miner-1',
      wallet: '1A1z7agoat2xSJvM4QqDvagRQHqzaChz7Q',
      worker: 'worker-1',
      pool_host: 'pool.example.com',
      pool_port: '3333',
      pool_pass: 'poolpass'
    }
    await postSave(body)
    expect(spy).toHaveBeenCalledTimes(1)
    const [url, init] = spy.mock.calls[0]
    expect(url).toBe('/save')
    expect(init.method).toBe('POST')
    expect(init.headers['Content-Type']).toBe('application/x-www-form-urlencoded')
    expect(init.body).toContain('ssid=test-ssid')
    expect(init.body).toContain('pass=password123')
  })

  it('encodes all fields in URLSearchParams', async () => {
    const spy = setFetch(200)
    const body: SaveBody = {
      ssid: 'network',
      pass: 'pass',
      hostname: 'host',
      wallet: 'wallet-addr',
      worker: 'worker',
      pool_host: 'host',
      pool_port: '3333',
      pool_pass: 'pass'
    }
    await postSave(body)
    const bodyStr = spy.mock.calls[0][1].body as string
    const params = new URLSearchParams(bodyStr)
    expect(params.get('ssid')).toBe('network')
    expect(params.get('wallet')).toBe('wallet-addr')
    expect(params.get('pool_port')).toBe('3333')
  })

  it('throws on non-OK response', async () => {
    setFetch(400)
    const body: SaveBody = {
      ssid: 'test',
      pass: 'test',
      hostname: 'host',
      wallet: 'addr',
      worker: 'worker',
      pool_host: 'host',
      pool_port: '3333',
      pool_pass: 'pass'
    }
    await expect(postSave(body)).rejects.toThrow('save failed')
  })

  it('handles empty string fields in SaveBody', async () => {
    const spy = setFetch(200)
    const body: SaveBody = {
      ssid: '',
      pass: '',
      hostname: '',
      wallet: '',
      worker: '',
      pool_host: '',
      pool_port: '',
      pool_pass: ''
    }
    await postSave(body)
    expect(spy).toHaveBeenCalledTimes(1)
    expect(spy.mock.calls[0][0]).toBe('/save')
  })

  it('handles special characters in field values', async () => {
    const spy = setFetch(200)
    const body: SaveBody = {
      ssid: 'Test Network & Special',
      pass: 'p@ssw0rd!',
      hostname: 'miner-1',
      wallet: '1A1z7agoat2xSJvM4QqDvagRQHqzaChz7Q',
      worker: 'worker-1',
      pool_host: 'pool.example.com:3333',
      pool_port: '3333',
      pool_pass: 'pass@word'
    }
    await postSave(body)
    const bodyStr = spy.mock.calls[0][1].body as string
    const params = new URLSearchParams(bodyStr)
    expect(params.get('ssid')).toContain('Special')
  })

  it('makes correct HTTP method and headers', async () => {
    const spy = setFetch(200)
    const body: SaveBody = {
      ssid: 'test',
      pass: 'test',
      hostname: 'host',
      wallet: 'wallet',
      worker: 'worker',
      pool_host: 'host',
      pool_port: '3333',
      pool_pass: 'pass'
    }
    await postSave(body)
    const [, init] = spy.mock.calls[0]
    expect(init.method).toBe('POST')
    expect(init.headers['Content-Type']).toBe('application/x-www-form-urlencoded')
  })
})
