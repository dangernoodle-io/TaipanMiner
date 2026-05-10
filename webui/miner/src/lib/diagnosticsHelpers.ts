import type { LogLevel } from './api'

export interface TagLevel {
  tag: string
  level: LogLevel
}

export function filterLines(lines: string[], filter: string): string[] {
  if (!filter) return lines
  const lower = filter.toLowerCase()
  return lines.filter((l) => l.toLowerCase().includes(lower))
}

/** Returns seconds until next retry, clamped to >= 0 */
export function retryInSeconds(nextRetryAt: number | null, now: number): number | null {
  if (nextRetryAt == null) return null
  return Math.max(0, Math.ceil((nextRetryAt - now) / 1000))
}

export function findLevelForTag(tagLevels: TagLevel[], tag: string): LogLevel | null {
  return tagLevels.find((t) => t.tag === tag)?.level ?? null
}
