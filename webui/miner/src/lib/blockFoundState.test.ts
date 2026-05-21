import { describe, it, expect, beforeEach } from 'vitest'

import { createBlockFoundState, BLOCK_FOUND_TOPIC } from './blockFoundState.svelte'

beforeEach(() => {
  localStorage.clear()
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

  it('exports the SSE topic name for the multiplexer', () => {
    expect(BLOCK_FOUND_TOPIC).toBe('block.found')
  })
})

describe('createBlockFoundState — handleMessage', () => {
  it('visible becomes true after a payload arrives', () => {
    const s = createBlockFoundState()
    s.handleMessage(JSON.stringify({ host: 'pool.example.com', port: 3333, share_diff: 1234.5 }))
    expect(s.lastFound).not.toBeNull()
    expect(s.lastFound!.host).toBe('pool.example.com')
    expect(s.lastFound!.port).toBe(3333)
    expect(s.lastFound!.share_diff).toBe(1234.5)
    expect(s.visible).toBe(true)
  })

  it('stamps receivedAt client-side on arrival', () => {
    const before = Date.now()
    const s = createBlockFoundState()
    s.handleMessage(JSON.stringify({ host: 'pool.example.com', port: 3333 }))
    const after = Date.now()
    expect(s.lastFound!.receivedAt).toBeGreaterThanOrEqual(before)
    expect(s.lastFound!.receivedAt).toBeLessThanOrEqual(after)
  })

  it('carries the firmware timestamp through to the payload', () => {
    const s = createBlockFoundState()
    s.handleMessage(JSON.stringify({ host: 'p', port: 1, share_diff: 1, timestamp: 1750000000 }))
    expect(s.lastFound!.timestamp).toBe(1750000000)
  })

  it('ignores malformed JSON', () => {
    const s = createBlockFoundState()
    expect(() => s.handleMessage('not json')).not.toThrow()
    expect(s.lastFound).toBeNull()
  })

  it('visible is false when an event matching dismissedKey arrives (SSE replay)', () => {
    localStorage.setItem('taipanminer.blockFound.dismissedKey', 'pool.example.com|3333|1234.5')
    const s = createBlockFoundState()
    s.handleMessage(JSON.stringify({ host: 'pool.example.com', port: 3333, share_diff: 1234.5 }))
    expect(s.lastFound).not.toBeNull()
    expect(s.visible).toBe(false)
  })

  it('visible is true when a new event with a different share_diff arrives', () => {
    localStorage.setItem('taipanminer.blockFound.dismissedKey', 'pool.example.com|3333|1234.5')
    const s = createBlockFoundState()
    s.handleMessage(JSON.stringify({ host: 'pool.example.com', port: 3333, share_diff: 9999.9 }))
    expect(s.visible).toBe(true)
  })
})

describe('createBlockFoundState — dismiss', () => {
  it('dismiss hides banner and persists the event key', () => {
    const s = createBlockFoundState()
    s.handleMessage(JSON.stringify({ host: 'pool.example.com', port: 3333, share_diff: 42 }))
    expect(s.visible).toBe(true)
    s.dismiss()
    expect(s.visible).toBe(false)
    expect(localStorage.getItem('taipanminer.blockFound.dismissedKey'))
      .toBe('pool.example.com|3333|42')
  })

  it('dismiss is a no-op when no event has been received', () => {
    const s = createBlockFoundState()
    s.dismiss()
    expect(s.dismissedKey).toBe('')
    expect(localStorage.getItem('taipanminer.blockFound.dismissedKey')).toBeNull()
  })

  it('a replayed event with identical payload stays hidden across instances', () => {
    const s1 = createBlockFoundState()
    s1.handleMessage(JSON.stringify({ host: 'pool.example.com', port: 3333, share_diff: 42 }))
    s1.dismiss()

    const s2 = createBlockFoundState()
    s2.handleMessage(JSON.stringify({ host: 'pool.example.com', port: 3333, share_diff: 42 }))
    expect(s2.visible).toBe(false)
  })
})
