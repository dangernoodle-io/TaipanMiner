import { describe, it, expect } from 'vitest'
import { formatVersion } from './version'

describe('formatVersion', () => {
  it('returns empty string for undefined or empty input', () => {
    expect(formatVersion()).toBe('')
    expect(formatVersion('')).toBe('')
    expect(formatVersion('   ')).toBe('')
  })

  it('leaves a tagged version with its single v prefix intact', () => {
    expect(formatVersion('v0.41.1')).toBe('v0.41.1')
  })

  it('collapses an accidentally doubled v prefix', () => {
    expect(formatVersion('vv0.41.1')).toBe('v0.41.1')
    expect(formatVersion('vvv1.2.3')).toBe('v1.2.3')
  })

  it('does not prepend a v to non-v versions (dev builds, bare semver)', () => {
    expect(formatVersion('dev-20260529023337')).toBe('dev-20260529023337')
    expect(formatVersion('0.41.1')).toBe('0.41.1')
  })

  it('trims surrounding whitespace', () => {
    expect(formatVersion('  v0.41.1  ')).toBe('v0.41.1')
  })
})
