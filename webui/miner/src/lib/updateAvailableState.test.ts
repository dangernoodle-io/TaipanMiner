import { describe, it, expect } from 'vitest'

import { createUpdateAvailableState, UPDATE_AVAILABLE_TOPIC } from './updateAvailableState.svelte'

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

  it('exports the SSE topic name for the multiplexer', () => {
    expect(UPDATE_AVAILABLE_TOPIC).toBe('update.available')
  })
})

describe('createUpdateAvailableState — handleMessage', () => {
  it('updates state when available=true payload arrives', () => {
    const s = createUpdateAvailableState()
    s.handleMessage(JSON.stringify({
      current: 'v0.21.2',
      latest: 'v9.9.9',
      download_url: 'https://example.com/release',
      available: true,
      ts: 1700000000,
    }))
    expect(s.available).toBe(true)
    expect(s.current).toBe('v0.21.2')
    expect(s.latest).toBe('v9.9.9')
    expect(s.downloadUrl).toBe('https://example.com/release')
  })

  it('updates state when available=false payload arrives', () => {
    const s = createUpdateAvailableState()
    s.handleMessage(JSON.stringify({
      available: true, latest: 'v9.9.9', current: 'v0.1.0', download_url: 'https://example.com/r',
    }))
    expect(s.available).toBe(true)
    s.handleMessage(JSON.stringify({
      available: false, latest: 'v0.1.0', current: 'v0.1.0', download_url: '',
    }))
    expect(s.available).toBe(false)
  })

  it('ignores malformed JSON', () => {
    const s = createUpdateAvailableState()
    expect(() => s.handleMessage('not json')).not.toThrow()
    expect(s.available).toBe(false)
  })

  it('partial payloads only touch the fields they include', () => {
    const s = createUpdateAvailableState()
    s.handleMessage(JSON.stringify({ available: true }))
    expect(s.available).toBe(true)
    expect(s.latest).toBeNull()
  })
})
