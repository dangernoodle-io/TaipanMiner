import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { SseClient } from './sse'

// Capture the onMessage callback so tests can drive it directly.
let capturedOnMessage: ((data: string) => void) | null = null

const mockStart = vi.fn()
const mockDestroy = vi.fn()

vi.mock('./sse', () => {
  return {
    SseClient: vi.fn().mockImplementation(function (this: unknown, opts: { onMessage: (data: string) => void }) {
      capturedOnMessage = opts.onMessage
      ;(this as Record<string, unknown>).start = mockStart
      ;(this as Record<string, unknown>).destroy = mockDestroy
    }),
  }
})

import { createUpdateAvailableState } from './updateAvailableState.svelte'

beforeEach(() => {
  vi.clearAllMocks()
  mockStart.mockReset()
  mockDestroy.mockReset()
  capturedOnMessage = null
})

afterEach(() => {
  vi.restoreAllMocks()
})

describe('createUpdateAvailableState — initial state', () => {
  it('available is false', () => {
    const s = createUpdateAvailableState()
    expect(s.available).toBe(false)
  })

  it('current is null', () => {
    const s = createUpdateAvailableState()
    expect(s.current).toBeNull()
  })

  it('latest is null', () => {
    const s = createUpdateAvailableState()
    expect(s.latest).toBeNull()
  })

  it('downloadUrl is null', () => {
    const s = createUpdateAvailableState()
    expect(s.downloadUrl).toBeNull()
  })
})

describe('createUpdateAvailableState — start / stop', () => {
  it('start creates SseClient with correct URL and calls start', () => {
    const s = createUpdateAvailableState()
    s.start()
    expect(SseClient).toHaveBeenCalledWith(
      expect.objectContaining({ url: '/api/events?topic=update.available' }),
    )
    expect(mockStart).toHaveBeenCalledOnce()
  })

  it('stop calls destroy on the client', () => {
    const s = createUpdateAvailableState()
    s.start()
    s.stop()
    expect(mockDestroy).toHaveBeenCalledOnce()
  })
})

describe('createUpdateAvailableState — onMessage payload', () => {
  it('updates state when available=true payload arrives', () => {
    const s = createUpdateAvailableState()
    s.start()

    capturedOnMessage!(
      JSON.stringify({
        current: 'v0.21.2',
        latest: 'v9.9.9',
        download_url: 'https://example.com/release',
        available: true,
        ts: 1700000000,
      }),
    )

    expect(s.available).toBe(true)
    expect(s.current).toBe('v0.21.2')
    expect(s.latest).toBe('v9.9.9')
    expect(s.downloadUrl).toBe('https://example.com/release')
  })

  it('updates state when available=false payload arrives', () => {
    const s = createUpdateAvailableState()
    s.start()

    // First set to true
    capturedOnMessage!(JSON.stringify({ available: true, latest: 'v9.9.9', current: 'v0.1.0', download_url: 'https://example.com/r' }))
    expect(s.available).toBe(true)

    // Then device reports up-to-date
    capturedOnMessage!(JSON.stringify({ available: false, latest: 'v0.1.0', current: 'v0.1.0', download_url: '' }))
    expect(s.available).toBe(false)
  })

  it('ignores malformed JSON', () => {
    const s = createUpdateAvailableState()
    s.start()
    expect(() => capturedOnMessage!('not json')).not.toThrow()
    expect(s.available).toBe(false)
  })

  it('ignores partial payload without crashing', () => {
    const s = createUpdateAvailableState()
    s.start()
    capturedOnMessage!(JSON.stringify({ available: true }))
    expect(s.available).toBe(true)
    expect(s.latest).toBeNull()
  })
})
