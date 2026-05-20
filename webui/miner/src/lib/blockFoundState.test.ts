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

import { createBlockFoundState } from './blockFoundState.svelte'

beforeEach(() => {
  vi.clearAllMocks()
  mockStart.mockReset()
  mockDestroy.mockReset()
  capturedOnMessage = null
  localStorage.clear()
})

afterEach(() => {
  vi.restoreAllMocks()
})

describe('createBlockFoundState — initial state', () => {
  it('lastFound is null', () => {
    const s = createBlockFoundState()
    expect(s.lastFound).toBeNull()
  })

  it('visible is false when no event received', () => {
    const s = createBlockFoundState()
    expect(s.visible).toBe(false)
  })

  it('dismissedAt is 0 when localStorage is empty', () => {
    const s = createBlockFoundState()
    expect(s.dismissedAt).toBe(0)
  })

  it('reads dismissedAt from localStorage on init', () => {
    localStorage.setItem('taipanminer.blockFound.dismissedAt', '1234567890')
    const s = createBlockFoundState()
    expect(s.dismissedAt).toBe(1234567890)
  })
})

describe('createBlockFoundState — start / stop', () => {
  it('start creates SseClient with block.found topic and eventName', () => {
    const s = createBlockFoundState()
    s.start()
    expect(SseClient).toHaveBeenCalledWith(
      expect.objectContaining({
        url: '/api/events?topic=block.found',
        eventName: 'block.found',
      }),
    )
    expect(mockStart).toHaveBeenCalledOnce()
  })

  it('stop calls destroy on the client', () => {
    const s = createBlockFoundState()
    s.start()
    s.stop()
    expect(mockDestroy).toHaveBeenCalledOnce()
  })
})

describe('createBlockFoundState — SSE event handling', () => {
  it('visible becomes true after block.found event arrives', () => {
    const s = createBlockFoundState()
    s.start()

    capturedOnMessage!(JSON.stringify({ host: 'pool.example.com', port: 3333, share_diff: 1234.5 }))

    expect(s.lastFound).not.toBeNull()
    expect(s.lastFound!.host).toBe('pool.example.com')
    expect(s.lastFound!.port).toBe(3333)
    expect(s.lastFound!.share_diff).toBe(1234.5)
    expect(s.visible).toBe(true)
  })

  it('stamps receivedAt client-side on arrival', () => {
    const before = Date.now()
    const s = createBlockFoundState()
    s.start()

    capturedOnMessage!(JSON.stringify({ host: 'pool.example.com', port: 3333 }))

    const after = Date.now()
    expect(s.lastFound!.receivedAt).toBeGreaterThanOrEqual(before)
    expect(s.lastFound!.receivedAt).toBeLessThanOrEqual(after)
  })

  it('ignores malformed JSON', () => {
    const s = createBlockFoundState()
    s.start()
    expect(() => capturedOnMessage!('not json')).not.toThrow()
    expect(s.lastFound).toBeNull()
  })

  it('visible is false when payload arrived before dismissedAt', () => {
    // Pre-seed localStorage with a future dismissedAt
    const future = Date.now() + 60_000
    localStorage.setItem('taipanminer.blockFound.dismissedAt', String(future))
    const s = createBlockFoundState()
    s.start()

    capturedOnMessage!(JSON.stringify({ host: 'pool.example.com', port: 3333 }))

    // receivedAt is now, dismissedAt is in the future → not visible
    expect(s.visible).toBe(false)
  })
})

describe('createBlockFoundState — dismiss', () => {
  it('dismiss hides banner and persists to localStorage', () => {
    const s = createBlockFoundState()
    s.start()

    capturedOnMessage!(JSON.stringify({ host: 'pool.example.com', port: 3333 }))
    expect(s.visible).toBe(true)

    s.dismiss()

    expect(s.visible).toBe(false)
    const stored = parseInt(localStorage.getItem('taipanminer.blockFound.dismissedAt') ?? '0', 10)
    expect(stored).toBeGreaterThan(0)
  })

  it('dismissedAt is updated to approximately now after dismiss', () => {
    const before = Date.now()
    const s = createBlockFoundState()
    s.start()

    capturedOnMessage!(JSON.stringify({ host: 'pool.example.com', port: 3333 }))
    s.dismiss()

    const after = Date.now()
    expect(s.dismissedAt).toBeGreaterThanOrEqual(before)
    expect(s.dismissedAt).toBeLessThanOrEqual(after)
  })

  it('fresh instance after dismiss reads dismissedAt from localStorage', () => {
    const s1 = createBlockFoundState()
    s1.start()
    capturedOnMessage!(JSON.stringify({ host: 'pool.example.com', port: 3333 }))
    s1.dismiss()
    const dismissedAt = s1.dismissedAt

    // Simulate reload: new state instance reads from localStorage
    const s2 = createBlockFoundState()
    expect(s2.dismissedAt).toBe(dismissedAt)
  })
})
