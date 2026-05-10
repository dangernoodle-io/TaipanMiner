import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { withTimeout } from './withTimeout'

beforeEach(() => {
  vi.useFakeTimers()
})

afterEach(() => {
  vi.useRealTimers()
})

describe('withTimeout', () => {
  it('resolves before timeout fires', async () => {
    const p = Promise.resolve(42)
    const result = await withTimeout(p, 5000, 'Test op')
    expect(result).toBe(42)
  })

  it('rejects with formatted message on timeout', async () => {
    const p = new Promise<never>(() => {}) // never resolves
    const wrapped = withTimeout(p, 3000, 'Chip sync')
    vi.advanceTimersByTime(3000)
    await expect(wrapped).rejects.toThrow('Chip sync timed out after 3s — try unplugging and replugging the device')
  })

  it('includes label in timeout message', async () => {
    const p = new Promise<never>(() => {})
    const wrapped = withTimeout(p, 5000, 'MAC read')
    vi.advanceTimersByTime(5000)
    await expect(wrapped).rejects.toThrow('MAC read timed out after 5s')
  })

  it('converts ms to seconds in message', async () => {
    const p = new Promise<never>(() => {})
    const wrapped = withTimeout(p, 30000, 'Flash detect')
    vi.advanceTimersByTime(30000)
    await expect(wrapped).rejects.toThrow('after 30s')
  })

  it('propagates rejection from inner promise before timeout', async () => {
    const p = Promise.reject(new Error('inner error'))
    await expect(withTimeout(p, 5000, 'Test')).rejects.toThrow('inner error')
  })

  it('does not fire timeout after promise resolves', async () => {
    let timeoutFired = false
    const p = Promise.resolve('ok')
    const wrapped = withTimeout(p, 1000, 'Test')
    const result = await wrapped
    vi.advanceTimersByTime(1000)
    expect(result).toBe('ok')
    expect(timeoutFired).toBe(false)
  })
})
