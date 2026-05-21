import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { SseClient } from './sse'

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

  it('dismissedKey is empty when localStorage is empty', () => {
    const s = createBlockFoundState()
    expect(s.dismissedKey).toBe('')
  })

  it('reads dismissedKey from localStorage on init', () => {
    localStorage.setItem('taipanminer.blockFound.dismissedKey', 'pool.example.com|3333|1234.5')
    const s = createBlockFoundState()
    expect(s.dismissedKey).toBe('pool.example.com|3333|1234.5')
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

  it('visible is false when an event matching the dismissedKey arrives (SSE replay)', () => {
    // Pre-seed the key the user dismissed in a previous page visit.
    localStorage.setItem('taipanminer.blockFound.dismissedKey', 'pool.example.com|3333|1234.5')
    const s = createBlockFoundState()
    s.start()

    // The SSE channel replays the same event after reconnect.
    capturedOnMessage!(JSON.stringify({ host: 'pool.example.com', port: 3333, share_diff: 1234.5 }))

    expect(s.lastFound).not.toBeNull()
    expect(s.visible).toBe(false)
  })

  it('visible is true when a new event with a different share_diff arrives', () => {
    localStorage.setItem('taipanminer.blockFound.dismissedKey', 'pool.example.com|3333|1234.5')
    const s = createBlockFoundState()
    s.start()

    capturedOnMessage!(JSON.stringify({ host: 'pool.example.com', port: 3333, share_diff: 9999.9 }))

    expect(s.visible).toBe(true)
  })
})

describe('createBlockFoundState — dismiss', () => {
  it('dismiss hides banner and persists the event key to localStorage', () => {
    const s = createBlockFoundState()
    s.start()

    capturedOnMessage!(JSON.stringify({ host: 'pool.example.com', port: 3333, share_diff: 42 }))
    expect(s.visible).toBe(true)

    s.dismiss()

    expect(s.visible).toBe(false)
    const stored = localStorage.getItem('taipanminer.blockFound.dismissedKey')
    expect(stored).toBe('pool.example.com|3333|42')
  })

  it('dismiss is a no-op when no event has been received', () => {
    const s = createBlockFoundState()
    s.start()
    s.dismiss()
    expect(s.dismissedKey).toBe('')
    expect(localStorage.getItem('taipanminer.blockFound.dismissedKey')).toBeNull()
  })

  it('fresh instance after dismiss reads dismissedKey from localStorage', () => {
    const s1 = createBlockFoundState()
    s1.start()
    capturedOnMessage!(JSON.stringify({ host: 'pool.example.com', port: 3333, share_diff: 7 }))
    s1.dismiss()
    const key = s1.dismissedKey

    const s2 = createBlockFoundState()
    expect(s2.dismissedKey).toBe(key)
  })

  it('after dismiss, a replayed event with identical payload stays hidden', () => {
    const s1 = createBlockFoundState()
    s1.start()
    capturedOnMessage!(JSON.stringify({ host: 'pool.example.com', port: 3333, share_diff: 42 }))
    s1.dismiss()

    // Simulate page navigation: brand-new state instance, SSE replays the event.
    const s2 = createBlockFoundState()
    s2.start()
    capturedOnMessage!(JSON.stringify({ host: 'pool.example.com', port: 3333, share_diff: 42 }))

    expect(s2.visible).toBe(false)
  })
})
