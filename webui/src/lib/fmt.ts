// Shared formatters so display is consistent across pages.

function pad(n: number): string { return n < 10 ? `0${n}` : `${n}` }

export function fmtTimestamp(d: Date | null | undefined): string {
  if (!d || isNaN(d.getTime())) return '—'
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}`
}

export function fmtUnixTs(seconds: number | null | undefined): string {
  if (seconds == null || seconds <= 0) return '—'
  return fmtTimestamp(new Date(seconds * 1000))
}

// Parses C's __DATE__ "Mon DD YYYY" + __TIME__ "HH:MM:SS" into a Date.
export function fmtBuildTime(dateStr: string | null | undefined, timeStr: string | null | undefined): string {
  if (!dateStr || !timeStr) return '—'
  const parsed = new Date(`${dateStr} ${timeStr}`)
  return fmtTimestamp(parsed)
}

export function fmtDuration(seconds: number | null | undefined): string {
  if (seconds == null || seconds < 0) return '—'
  if (seconds < 60) return `${Math.floor(seconds)}s`
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m ${Math.floor(seconds % 60)}s`
  const h = Math.floor(seconds / 3600)
  const m = Math.floor((seconds % 3600) / 60)
  if (h < 24) return `${h}h ${m}m`
  return `${Math.floor(h / 24)}d ${h % 24}h`
}

export function fmtRelative(seconds: number | null | undefined): string {
  if (seconds == null || seconds < 0) return '—'
  if (seconds < 60) return `${Math.floor(seconds)}s ago`
  if (seconds < 3600) return `${Math.floor(seconds / 60)}m ago`
  if (seconds < 86400) return `${Math.floor(seconds / 3600)}h ago`
  return `${Math.floor(seconds / 86400)}d ago`
}

export function fmtBytes(b: number | null | undefined): string {
  if (b == null) return '—'
  if (b < 1024) return `${b} B`
  if (b < 1024 * 1024) return `${(b / 1024).toFixed(0)} KB`
  return `${(b / 1024 / 1024).toFixed(1)} MB`
}
