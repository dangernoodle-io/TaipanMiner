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

export function fmtHashGhs(ghs: number | null | undefined): string {
  if (ghs === null || ghs === undefined || isNaN(ghs)) return '—'
  if (ghs >= 1000) return (ghs / 1000).toFixed(2) + ' TH/s'
  if (ghs >= 1) return ghs.toFixed(1) + ' GH/s'
  if (ghs >= 0.001) return (ghs * 1000).toFixed(1) + ' MH/s'
  return (ghs * 1e6).toFixed(1) + ' kH/s'
}

// SI-suffix difficulty formatter (large numbers).
export function fmtNetDiff(d: number): string {
  if (!Number.isFinite(d) || d <= 0) return '—'
  if (d >= 1e12) return (d / 1e12).toFixed(2) + 'T'
  if (d >= 1e9)  return (d / 1e9).toFixed(2) + 'G'
  if (d >= 1e6)  return (d / 1e6).toFixed(2) + 'M'
  if (d >= 1e3)  return (d / 1e3).toFixed(2) + 'k'
  return d.toFixed(0)
}

// Compact difficulty formatter without sign check (used for share diffs).
export function fmtDiff(d: number): string {
  if (d >= 1e9) return (d / 1e9).toFixed(2) + 'G'
  if (d >= 1e6) return (d / 1e6).toFixed(2) + 'M'
  if (d >= 1e3) return (d / 1e3).toFixed(2) + 'k'
  return d.toFixed(0)
}

export function fmtBtc(sats: number): string {
  return (sats / 1e8).toFixed(4) + ' BTC'
}

// Hex-epoch relative formatter.
export function fmtNtimeAge(ntimeHex: string): string | null {
  const t = parseInt(ntimeHex, 16)
  if (!Number.isFinite(t) || t <= 0) return null
  const ago = Math.floor(Date.now() / 1000) - t
  if (ago < 0) return 'now'
  if (ago < 60) return `${ago}s ago`
  if (ago < 3600) return `${Math.floor(ago / 60)}m ago`
  return `${Math.floor(ago / 3600)}h ago`
}

// 8+6 address truncation (coinbase payout addresses).
export function truncAddr(a: string): string {
  if (!a) return '—'
  if (a.length <= 16) return a
  return `${a.slice(0, 8)}…${a.slice(-6)}`
}

// 6+4 wallet truncation (pool row display).
export function truncWallet(w: string | undefined): string {
  if (!w) return '—'
  if (w.length <= 14) return w
  return `${w.slice(0, 6)}…${w.slice(-4)}`
}

export function fmtPct(v: number | null): string {
  return v == null ? '—' : v.toFixed(2) + '%'
}

export function fmtGhsNum(v: number | null): string {
  return v == null ? '—' : v >= 1000 ? (v/1000).toFixed(2) : v.toFixed(0)
}

export function fmtGhsUnit(v: number | null): string {
  return v == null ? '' : v >= 1000 ? 'TH/s' : 'GH/s'
}

export function rssiBars(r: number | null | undefined): string {
  if (r == null) return ''
  if (r >= -55) return '▮▮▮▮'
  if (r >= -65) return '▮▮▮▯'
  if (r >= -75) return '▮▮▯▯'
  if (r >= -85) return '▮▯▯▯'
  return '▯▯▯▯'
}
