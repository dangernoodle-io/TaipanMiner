import { describe, it, expect } from 'vitest'
import { filterLines, retryInSeconds, findLevelForTag, type TagLevel } from './diagnosticsHelpers'

describe('filterLines', () => {
  it('returns all lines when filter is empty', () => {
    expect(filterLines(['a', 'b', 'c'], '')).toEqual(['a', 'b', 'c'])
  })

  it('returns empty when no match', () => {
    expect(filterLines(['foo', 'bar'], 'zzz')).toEqual([])
  })

  it('returns matching lines on single match', () => {
    expect(filterLines(['foo', 'bar', 'baz'], 'foo')).toEqual(['foo'])
  })

  it('returns all matching lines on multi match', () => {
    expect(filterLines(['foo', 'foobar', 'baz'], 'foo')).toEqual(['foo', 'foobar'])
  })

  it('is case-insensitive', () => {
    expect(filterLines(['INFO hello', 'DEBUG world'], 'info')).toEqual(['INFO hello'])
  })

  it('matches substring in middle of line', () => {
    expect(filterLines(['start ERROR end'], 'error')).toEqual(['start ERROR end'])
  })

  it('returns original array reference when filter is empty', () => {
    const arr = ['a', 'b']
    expect(filterLines(arr, '')).toBe(arr)
  })

  it('handles empty lines array', () => {
    expect(filterLines([], 'anything')).toEqual([])
  })
})

describe('retryInSeconds', () => {
  it('returns null when nextRetryAt is null', () => {
    expect(retryInSeconds(null, Date.now())).toBeNull()
  })

  it('returns positive seconds for future retry', () => {
    const now = 1000000
    expect(retryInSeconds(now + 3000, now)).toBe(3)
  })

  it('rounds up fractional seconds', () => {
    const now = 1000000
    // 2500ms remaining → ceil = 3
    expect(retryInSeconds(now + 2500, now)).toBe(3)
  })

  it('returns 0 when retry is in the past', () => {
    const now = 1000000
    expect(retryInSeconds(now - 5000, now)).toBe(0)
  })

  it('returns 0 for exactly now', () => {
    const now = 1000000
    expect(retryInSeconds(now, now)).toBe(0)
  })

  it('handles far-future timestamp', () => {
    const now = 0
    expect(retryInSeconds(120000, now)).toBe(120)
  })
})

describe('findLevelForTag', () => {
  const tagLevels: TagLevel[] = [
    { tag: '*', level: 'info' },
    { tag: 'wifi', level: 'warn' },
    { tag: 'stratum', level: 'debug' },
  ]

  it('returns level for a matching tag', () => {
    expect(findLevelForTag(tagLevels, 'wifi')).toBe('warn')
  })

  it('returns level for the wildcard tag', () => {
    expect(findLevelForTag(tagLevels, '*')).toBe('info')
  })

  it('returns null when tag is not found', () => {
    expect(findLevelForTag(tagLevels, 'asic')).toBeNull()
  })

  it('returns null on empty list', () => {
    expect(findLevelForTag([], 'anything')).toBeNull()
  })

  it('returns null when tag is empty string and not in list', () => {
    expect(findLevelForTag(tagLevels, '')).toBeNull()
  })
})
