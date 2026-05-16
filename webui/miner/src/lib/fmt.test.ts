import { describe, it, expect, vi, afterEach } from 'vitest'
import { fmtHashGhs, fmtBytes, fmtUnixTs, fmtBuildTime, fmtDuration, fmtRelative, fmtTimestamp, fmtNetDiff, fmtPoolDiff, fmtDiff, fmtBtc, fmtNtimeAge, truncAddr, truncWallet, fmtPct, fmtGhsNum, fmtGhsUnit, rssiBars } from './fmt'

describe('fmt', () => {
  describe('fmtTimestamp', () => {
    it('returns dash for null/undefined', () => {
      expect(fmtTimestamp(null)).toBe('—')
      expect(fmtTimestamp(undefined)).toBe('—')
    })

    it('returns dash for invalid Date', () => {
      expect(fmtTimestamp(new Date('invalid'))).toBe('—')
    })

    it('formats valid Date', () => {
      const d = new Date(2025, 3, 24, 14, 30, 0)
      const result = fmtTimestamp(d)
      expect(result).toMatch(/2025-04-24 \d{2}:\d{2}/)
    })

    it('pads single-digit months/days/hours/minutes', () => {
      const d = new Date(2025, 0, 5, 9, 5, 0)
      const result = fmtTimestamp(d)
      expect(result).toContain('2025-01-05')
      expect(result).toContain('09:05')
    })
  })

  describe('fmtUnixTs', () => {
    it('returns dash for null/undefined', () => {
      expect(fmtUnixTs(null)).toBe('—')
      expect(fmtUnixTs(undefined)).toBe('—')
    })

    it('returns dash for zero and negative', () => {
      expect(fmtUnixTs(0)).toBe('—')
      expect(fmtUnixTs(-100)).toBe('—')
    })

    it('formats positive epoch seconds', () => {
      const ts = 1640995200 // 2022-01-01 00:00:00 UTC
      const result = fmtUnixTs(ts)
      // Verify it returns a formatted date string (timezone-dependent)
      expect(result).toMatch(/\d{4}-\d{2}-\d{2} \d{2}:\d{2}/)
    })
  })

  describe('fmtBuildTime', () => {
    it('returns dash for null/undefined inputs', () => {
      expect(fmtBuildTime(null, 'HH:MM:SS')).toBe('—')
      expect(fmtBuildTime('Mon DD YYYY', null)).toBe('—')
      expect(fmtBuildTime(undefined, '12:34:56')).toBe('—')
      expect(fmtBuildTime('Apr 24 2025', undefined)).toBe('—')
    })

    it('formats valid C build time strings', () => {
      // Note: Date parsing is locale-dependent, so we just verify format
      const result = fmtBuildTime('Apr 24 2025', '14:30:45')
      expect(result).toMatch(/\d{4}-\d{2}-\d{2} \d{2}:\d{2}/)
    })
  })

  describe('fmtDuration', () => {
    it('returns dash for null/undefined/negative', () => {
      expect(fmtDuration(null)).toBe('—')
      expect(fmtDuration(undefined)).toBe('—')
      expect(fmtDuration(-10)).toBe('—')
    })

    it('formats seconds < 60', () => {
      expect(fmtDuration(0)).toBe('0s')
      expect(fmtDuration(45)).toBe('45s')
      expect(fmtDuration(59)).toBe('59s')
    })

    it('formats minutes < 60', () => {
      expect(fmtDuration(60)).toBe('1m 0s')
      expect(fmtDuration(90)).toBe('1m 30s')
      expect(fmtDuration(3599)).toBe('59m 59s')
    })

    it('formats hours < 24', () => {
      expect(fmtDuration(3600)).toBe('1h 0m')
      expect(fmtDuration(5400)).toBe('1h 30m')
      expect(fmtDuration(86399)).toBe('23h 59m')
    })

    it('formats days >= 24', () => {
      expect(fmtDuration(86400)).toBe('1d 0h')
      expect(fmtDuration(90000)).toBe('1d 1h')
      expect(fmtDuration(172800)).toBe('2d 0h')
    })
  })

  describe('fmtRelative', () => {
    it('returns dash for null/undefined/negative', () => {
      expect(fmtRelative(null)).toBe('—')
      expect(fmtRelative(undefined)).toBe('—')
      expect(fmtRelative(-10)).toBe('—')
    })

    it('formats seconds < 60', () => {
      expect(fmtRelative(0)).toBe('0s ago')
      expect(fmtRelative(30)).toBe('30s ago')
      expect(fmtRelative(59)).toBe('59s ago')
    })

    it('formats minutes < 60', () => {
      expect(fmtRelative(60)).toBe('1m ago')
      expect(fmtRelative(90)).toBe('1m ago')
      expect(fmtRelative(3599)).toBe('59m ago')
    })

    it('formats hours < 24', () => {
      expect(fmtRelative(3600)).toBe('1h ago')
      expect(fmtRelative(7200)).toBe('2h ago')
      expect(fmtRelative(86399)).toBe('23h ago')
    })

    it('formats days >= 24', () => {
      expect(fmtRelative(86400)).toBe('1d ago')
      expect(fmtRelative(172800)).toBe('2d ago')
    })
  })

  describe('fmtBytes', () => {
    it('returns dash for null/undefined', () => {
      expect(fmtBytes(null)).toBe('—')
      expect(fmtBytes(undefined)).toBe('—')
    })

    it('formats bytes < 1024', () => {
      expect(fmtBytes(0)).toBe('0 B')
      expect(fmtBytes(1)).toBe('1 B')
      expect(fmtBytes(1023)).toBe('1023 B')
    })

    it('formats kilobytes (1KB - 1MB)', () => {
      expect(fmtBytes(1024)).toBe('1 KB')
      expect(fmtBytes(1024 * 512)).toBe('512 KB')
      expect(fmtBytes(1024 * 1024 - 1)).toBe('1024 KB')
    })

    it('formats megabytes >= 1MB', () => {
      expect(fmtBytes(1024 * 1024)).toBe('1.0 MB')
      expect(fmtBytes(1024 * 1024 * 2.5)).toBe('2.5 MB')
      expect(fmtBytes(1024 * 1024 * 1024)).toBe('1024.0 MB')
    })
  })

  describe('fmtNetDiff', () => {
    it('returns dash for non-finite or non-positive', () => {
      expect(fmtNetDiff(NaN)).toBe('—')
      expect(fmtNetDiff(Infinity)).toBe('—')
      expect(fmtNetDiff(0)).toBe('—')
      expect(fmtNetDiff(-1)).toBe('—')
    })

    it('formats values < 1k as plain integer', () => {
      expect(fmtNetDiff(1)).toBe('1')
      expect(fmtNetDiff(999)).toBe('999')
    })

    it('formats values in k range', () => {
      expect(fmtNetDiff(1e3)).toBe('1.00k')
      expect(fmtNetDiff(1500)).toBe('1.50k')
      expect(fmtNetDiff(999999)).toBe('1000.00k')
    })

    it('formats values in M range', () => {
      expect(fmtNetDiff(1e6)).toBe('1.00M')
      expect(fmtNetDiff(2.5e6)).toBe('2.50M')
    })

    it('formats values in G range', () => {
      expect(fmtNetDiff(1e9)).toBe('1.00G')
      expect(fmtNetDiff(85.3e9)).toBe('85.30G')
    })

    it('formats values in T range', () => {
      expect(fmtNetDiff(1e12)).toBe('1.00T')
      expect(fmtNetDiff(88.12e12)).toBe('88.12T')
    })
  })

  describe('fmtPoolDiff', () => {
    it('returns dash for null/undefined/non-finite', () => {
      expect(fmtPoolDiff(null)).toBe('—')
      expect(fmtPoolDiff(undefined)).toBe('—')
      expect(fmtPoolDiff(NaN)).toBe('—')
      expect(fmtPoolDiff(Infinity)).toBe('—')
    })

    it('rounds values >= 1 to integer', () => {
      expect(fmtPoolDiff(1)).toBe('1')
      expect(fmtPoolDiff(1.9)).toBe('2')
      expect(fmtPoolDiff(100)).toBe('100')
      expect(fmtPoolDiff(65536)).toBe('65536')
    })

    it('formats sub-1 values with 2 sig figs', () => {
      expect(fmtPoolDiff(0.01)).toBe('0.010')
      expect(fmtPoolDiff(0.5)).toBe('0.50')
    })
  })

  describe('fmtDiff', () => {
    it('formats plain values < 1k', () => {
      expect(fmtDiff(0)).toBe('0')
      expect(fmtDiff(1)).toBe('1')
      expect(fmtDiff(999)).toBe('999')
    })

    it('preserves sub-1 values at 2 sig figs', () => {
      // Regression: SW miners (tdongle) routinely produce sub-1 share diffs.
      // Old fmtDiff rounded them via toFixed(0) and the dashboard displayed "0".
      expect(fmtDiff(0.179)).toBe('0.18')
      expect(fmtDiff(0.5)).toBe('0.50')
      expect(fmtDiff(0.99)).toBe('0.99')
    })

    it('formats k range', () => {
      expect(fmtDiff(1000)).toBe('1.00k')
      expect(fmtDiff(1500)).toBe('1.50k')
    })

    it('formats M range', () => {
      expect(fmtDiff(1e6)).toBe('1.00M')
      expect(fmtDiff(2.25e6)).toBe('2.25M')
    })

    it('formats G range', () => {
      expect(fmtDiff(1e9)).toBe('1.00G')
      expect(fmtDiff(3.5e9)).toBe('3.50G')
    })
  })

  describe('fmtBtc', () => {
    it('converts satoshis to BTC with 4 decimal places', () => {
      expect(fmtBtc(0)).toBe('0.0000 BTC')
      expect(fmtBtc(1e8)).toBe('1.0000 BTC')
      expect(fmtBtc(312500000)).toBe('3.1250 BTC')
      expect(fmtBtc(1)).toBe('0.0000 BTC')
    })

    it('handles sub-satoshi rounding', () => {
      expect(fmtBtc(50000000)).toBe('0.5000 BTC')
    })
  })

  describe('fmtNtimeAge', () => {
    afterEach(() => {
      vi.restoreAllMocks()
    })

    it('returns null for invalid/non-positive ntime', () => {
      expect(fmtNtimeAge('00000000')).toBeNull()
      expect(fmtNtimeAge('zzzzzzzz')).toBeNull()
    })

    it('returns "now" for future ntime', () => {
      const futureTs = Math.floor(Date.now() / 1000) + 3600
      const hex = futureTs.toString(16).padStart(8, '0')
      expect(fmtNtimeAge(hex)).toBe('now')
    })

    it('formats recent ntime as seconds ago', () => {
      const nowTs = Math.floor(Date.now() / 1000) - 30
      const hex = nowTs.toString(16).padStart(8, '0')
      expect(fmtNtimeAge(hex)).toBe('30s ago')
    })

    it('formats ntime in minutes range', () => {
      const ts = Math.floor(Date.now() / 1000) - 300
      const hex = ts.toString(16).padStart(8, '0')
      expect(fmtNtimeAge(hex)).toBe('5m ago')
    })

    it('formats ntime in hours range', () => {
      const ts = Math.floor(Date.now() / 1000) - 7200
      const hex = ts.toString(16).padStart(8, '0')
      expect(fmtNtimeAge(hex)).toBe('2h ago')
    })
  })

  describe('truncAddr', () => {
    it('returns dash for empty/falsy', () => {
      expect(truncAddr('')).toBe('—')
    })

    it('returns full string if <= 16 chars', () => {
      expect(truncAddr('abc')).toBe('abc')
      expect(truncAddr('1234567890123456')).toBe('1234567890123456')
    })

    it('truncates long addresses with ellipsis', () => {
      const addr = 'bc1qar0srrr7xfkvy5l643lydnw9re59gtzzwf5mdq'
      const result = truncAddr(addr)
      expect(result).toContain('…')
      expect(result.startsWith(addr.slice(0, 8))).toBe(true)
      expect(result.endsWith(addr.slice(-6))).toBe(true)
    })
  })

  describe('truncWallet', () => {
    it('returns dash for undefined/empty', () => {
      expect(truncWallet(undefined)).toBe('—')
      expect(truncWallet('')).toBe('—')
    })

    it('returns full string if <= 14 chars', () => {
      expect(truncWallet('shortname')).toBe('shortname')
      expect(truncWallet('12345678901234')).toBe('12345678901234')
    })

    it('truncates long wallet with ellipsis', () => {
      const wallet = 'bc1pexamplewallet1234567890'
      const result = truncWallet(wallet)
      expect(result).toContain('…')
      expect(result.startsWith(wallet.slice(0, 6))).toBe(true)
      expect(result.endsWith(wallet.slice(-4))).toBe(true)
    })
  })

  describe('fmtPct', () => {
    it('returns dash for null', () => {
      expect(fmtPct(null)).toBe('—')
    })

    it('formats percentage with 2 decimals', () => {
      expect(fmtPct(0)).toBe('0.00%')
      expect(fmtPct(100)).toBe('100.00%')
      expect(fmtPct(3.14159)).toBe('3.14%')
      expect(fmtPct(0.5)).toBe('0.50%')
    })
  })

  describe('fmtGhsNum', () => {
    it('returns dash for null', () => {
      expect(fmtGhsNum(null)).toBe('—')
    })

    it('formats TH/s range (>= 1000 GH/s)', () => {
      expect(fmtGhsNum(1000)).toBe('1.00')
      expect(fmtGhsNum(1200)).toBe('1.20')
    })

    it('formats GH/s range (>= 1)', () => {
      expect(fmtGhsNum(1)).toBe('1')
      expect(fmtGhsNum(485)).toBe('485')
    })

    it('formats MH/s range (0.001 to <1)', () => {
      expect(fmtGhsNum(0.001)).toBe('1.0')
      expect(fmtGhsNum(0.5)).toBe('500.0')
    })

    it('formats kH/s range (< 0.001)', () => {
      expect(fmtGhsNum(0.0001)).toBe('100.0')
    })
  })

  describe('fmtGhsUnit', () => {
    it('returns empty string for null', () => {
      expect(fmtGhsUnit(null)).toBe('')
    })

    it('returns TH/s for >= 1000', () => {
      expect(fmtGhsUnit(1000)).toBe('TH/s')
      expect(fmtGhsUnit(5000)).toBe('TH/s')
    })

    it('returns GH/s for >= 1', () => {
      expect(fmtGhsUnit(1)).toBe('GH/s')
      expect(fmtGhsUnit(999)).toBe('GH/s')
    })

    it('returns MH/s for 0.001 to <1', () => {
      expect(fmtGhsUnit(0.001)).toBe('MH/s')
      expect(fmtGhsUnit(0.999)).toBe('MH/s')
    })

    it('returns kH/s for < 0.001', () => {
      expect(fmtGhsUnit(0.0001)).toBe('kH/s')
    })
  })

  describe('rssiBars', () => {
    it('returns empty string for null/undefined', () => {
      expect(rssiBars(null)).toBe('')
      expect(rssiBars(undefined)).toBe('')
    })

    it('returns 4 filled bars for very strong signal (>= -55)', () => {
      expect(rssiBars(-55)).toBe('▮▮▮▮')
      expect(rssiBars(-30)).toBe('▮▮▮▮')
    })

    it('returns 3 bars for good signal (-65 to -56)', () => {
      expect(rssiBars(-65)).toBe('▮▮▮▯')
      expect(rssiBars(-60)).toBe('▮▮▮▯')
    })

    it('returns 2 bars for fair signal (-75 to -66)', () => {
      expect(rssiBars(-75)).toBe('▮▮▯▯')
      expect(rssiBars(-70)).toBe('▮▮▯▯')
    })

    it('returns 1 bar for weak signal (-85 to -76)', () => {
      expect(rssiBars(-85)).toBe('▮▯▯▯')
      expect(rssiBars(-80)).toBe('▮▯▯▯')
    })

    it('returns 0 bars for very weak signal (< -85)', () => {
      expect(rssiBars(-86)).toBe('▯▯▯▯')
      expect(rssiBars(-100)).toBe('▯▯▯▯')
    })
  })

  describe('fmtHashGhs', () => {
    it('returns dash for NaN/undefined', () => {
      expect(fmtHashGhs(NaN)).toBe('—')
      expect(fmtHashGhs(undefined)).toBe('—')
    })

    it('formats kH/s (< 0.001 GH/s)', () => {
      expect(fmtHashGhs(0.0005)).toBe('500.0 kH/s')
      expect(fmtHashGhs(0.0001)).toBe('100.0 kH/s')
    })

    it('formats MH/s (0.001 - 1 GH/s)', () => {
      expect(fmtHashGhs(0.001)).toBe('1.0 MH/s')
      expect(fmtHashGhs(0.5)).toBe('500.0 MH/s')
      expect(fmtHashGhs(0.999)).toBe('999.0 MH/s')
    })

    it('formats GH/s (1 - 1000 GH/s)', () => {
      expect(fmtHashGhs(1)).toBe('1.0 GH/s')
      expect(fmtHashGhs(500)).toBe('500.0 GH/s')
      expect(fmtHashGhs(999.9)).toBe('999.9 GH/s')
    })

    it('formats TH/s (>= 1000 GH/s)', () => {
      expect(fmtHashGhs(1000)).toBe('1.00 TH/s')
      expect(fmtHashGhs(1200)).toBe('1.20 TH/s')
      expect(fmtHashGhs(10000)).toBe('10.00 TH/s')
    })
  })
})
