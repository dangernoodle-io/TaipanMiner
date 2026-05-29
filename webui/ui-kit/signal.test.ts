import { describe, it, expect } from 'vitest'
import { signalBars } from './signal'

describe('signalBars', () => {
  it('returns 3 for strong signals (>= -67 dBm)', () => {
    expect(signalBars(-30)).toBe(3)
    expect(signalBars(-67)).toBe(3)
  })

  it('returns 2 for good signals (-68..-75 dBm)', () => {
    expect(signalBars(-68)).toBe(2)
    // the AP that started this: a normal home AP at ~-71/-74 must not read as 1 bar
    expect(signalBars(-71)).toBe(2)
    expect(signalBars(-74)).toBe(2)
    expect(signalBars(-75)).toBe(2)
  })

  it('returns 1 for weak signals (-76..-85 dBm)', () => {
    expect(signalBars(-76)).toBe(1)
    expect(signalBars(-83)).toBe(1)
    expect(signalBars(-85)).toBe(1)
  })

  it('returns 0 below the usable floor (< -85 dBm)', () => {
    expect(signalBars(-86)).toBe(0)
    expect(signalBars(-100)).toBe(0)
  })
})
