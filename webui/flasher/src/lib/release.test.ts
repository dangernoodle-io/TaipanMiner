import { describe, it, expect, vi, beforeEach } from 'vitest'
import { loadManifest, loadAsset, type ManifestAsset } from './release'

function setFetch(status: number, body: unknown = {}): ReturnType<typeof vi.fn> {
  const bodyStr = typeof body === 'string' ? body : JSON.stringify(body)
  const spy = vi.fn(async () => new Response(bodyStr, { status }))
  ;(globalThis as unknown as { fetch: typeof fetch }).fetch = spy as unknown as typeof fetch
  return spy
}

beforeEach(() => {
  vi.clearAllMocks()
})

const fakeManifest = {
  tag: 'v1.2.3',
  publishedAt: '2025-01-01T00:00:00Z',
  assets: {
    'tdongle-s3': { file: 'fw.bin', size: 1024, sha256: 'abc' }
  }
}

describe('loadManifest', () => {
  it('fetches firmware/manifest.json and returns parsed JSON', async () => {
    const spy = setFetch(200, fakeManifest)
    const result = await loadManifest()
    expect(spy.mock.calls[0][0]).toBe('firmware/manifest.json')
    expect(result.tag).toBe('v1.2.3')
    expect(result.assets['tdongle-s3'].file).toBe('fw.bin')
  })

  it('throws a helpful 404 message', async () => {
    setFetch(404)
    await expect(loadManifest()).rejects.toThrow('Firmware manifest not found')
  })

  it('throws a generic error for other non-ok statuses', async () => {
    setFetch(500, '')
    await expect(loadManifest()).rejects.toThrow('Failed to load manifest: 500')
  })
})

describe('loadAsset', () => {
  async function sha256Hex(bytes: Uint8Array): Promise<string> {
    const buf = await crypto.subtle.digest('SHA-256', bytes as BufferSource)
    return [...new Uint8Array(buf)].map(b => b.toString(16).padStart(2, '0')).join('')
  }

  async function makeAsset(content: Uint8Array): Promise<ManifestAsset> {
    const sha256 = await sha256Hex(content)
    return { file: 'test.bin', size: content.length, sha256 }
  }

  it('downloads asset and returns Uint8Array via arrayBuffer fallback (no body)', async () => {
    const content = new Uint8Array([1, 2, 3, 4])
    const asset = await makeAsset(content)
    vi.fn()
    const spy = vi.fn(async () => {
      const resp = new Response(content.buffer as ArrayBuffer, { status: 200 })
      Object.defineProperty(resp, 'body', { value: null })
      return resp
    })
    ;(globalThis as unknown as { fetch: typeof fetch }).fetch = spy as unknown as typeof fetch
    const result = await loadAsset(asset)
    expect(result).toBeInstanceOf(Uint8Array)
    expect(result).toEqual(content)
  })

  it('calls onProgress during streaming download', async () => {
    const content = new Uint8Array([10, 20, 30])
    const asset = await makeAsset(content)
    const progressCalls: Array<[number, number]> = []

    const spy = vi.fn(async () => {
      const chunks = [new Uint8Array([10]), new Uint8Array([20, 30])]
      let i = 0
      const stream = new ReadableStream({
        pull(controller) {
          if (i < chunks.length) {
            controller.enqueue(chunks[i++])
          } else {
            controller.close()
          }
        }
      })
      return new Response(stream, { status: 200 })
    })
    ;(globalThis as unknown as { fetch: typeof fetch }).fetch = spy as unknown as typeof fetch

    await loadAsset(asset, (loaded, total) => {
      progressCalls.push([loaded, total])
    })

    expect(progressCalls.length).toBeGreaterThan(0)
    expect(progressCalls[progressCalls.length - 1][0]).toBe(3)
  })

  it('throws on sha256 mismatch', async () => {
    const content = new Uint8Array([1, 2, 3])
    const asset: ManifestAsset = { file: 'test.bin', size: 3, sha256: 'deadbeef' }
    vi.fn()
    const spy = vi.fn(async () => {
      const resp = new Response(content.buffer as ArrayBuffer, { status: 200 })
      Object.defineProperty(resp, 'body', { value: null })
      return resp
    })
    ;(globalThis as unknown as { fetch: typeof fetch }).fetch = spy as unknown as typeof fetch
    await expect(loadAsset(asset)).rejects.toThrow('Integrity check failed')
  })

  it('throws on non-ok response', async () => {
    setFetch(404)
    const asset: ManifestAsset = { file: 'missing.bin', size: 0, sha256: '' }
    await expect(loadAsset(asset)).rejects.toThrow('Download failed: 404')
  })

  it('calls onProgress with full size in arrayBuffer fallback', async () => {
    const content = new Uint8Array([5, 6, 7, 8])
    const asset = await makeAsset(content)
    const progressCalls: Array<[number, number]> = []
    const spy = vi.fn(async () => {
      const resp = new Response(content.buffer as ArrayBuffer, { status: 200 })
      Object.defineProperty(resp, 'body', { value: null })
      return resp
    })
    ;(globalThis as unknown as { fetch: typeof fetch }).fetch = spy as unknown as typeof fetch
    await loadAsset(asset, (loaded, total) => { progressCalls.push([loaded, total]) })
    expect(progressCalls).toEqual([[4, 4]])
  })
})
