/**
 * Tests for coinbase.ts — bech32 encoding + coinbase transaction parsing.
 *
 * Bech32 test vectors sourced from BIP-173 (P2WPKH/P2WSH, bech32) and
 * BIP-350 (P2TR, bech32m):
 *   https://github.com/bitcoin/bips/blob/master/bip-0173.mediawiki#Test_vectors
 *   https://github.com/bitcoin/bips/blob/master/bip-0350.mediawiki#Test_vectors
 *
 * nbits/difficulty test vectors verified against Bitcoin Core's
 * GetDifficulty() implementation and block 0 genesis nbits (0x1d00ffff → 1.0).
 *
 * Coinbase hex fragments are synthetic and annotated with byte-level layout
 * to allow independent verification.
 */

import { describe, it, expect } from 'vitest'
import {
  BECH32_CHARS,
  bech32Polymod,
  bech32HrpExpand,
  bech32Checksum,
  convertBits,
  segwitAddress,
  nbitsToDifficulty,
  coinbaseTag,
  coinbaseHeight,
  coinbaseTotalReward,
  coinbasePayoutSpk,
} from './coinbase'

// ---------------------------------------------------------------------------
// BECH32_CHARS
// ---------------------------------------------------------------------------

describe('BECH32_CHARS', () => {
  it('has 32 unique characters', () => {
    expect(BECH32_CHARS).toHaveLength(32)
    expect(new Set(BECH32_CHARS).size).toBe(32)
  })

  it('starts with qpzry9x8gf2tvdw0s3jn54khce6mua7l', () => {
    expect(BECH32_CHARS).toBe('qpzry9x8gf2tvdw0s3jn54khce6mua7l')
  })
})

// ---------------------------------------------------------------------------
// bech32HrpExpand
// ---------------------------------------------------------------------------

describe('bech32HrpExpand', () => {
  it('produces correct expansion for "bc" (BIP-173 reference)', () => {
    // 'b' = 0x62, 'c' = 0x63
    // high bits: [3, 3], separator: [0], low bits: [2, 3]
    expect(bech32HrpExpand('bc')).toEqual([3, 3, 0, 2, 3])
  })

  it('produces correct expansion for "tb" (testnet)', () => {
    // 't' = 0x74, 'b' = 0x62
    // high bits: [3, 3], sep: [0], low bits: [20, 2]
    expect(bech32HrpExpand('tb')).toEqual([3, 3, 0, 20, 2])
  })

  it('length is 2*hrp.length + 1', () => {
    const result = bech32HrpExpand('bc')
    expect(result).toHaveLength(2 * 2 + 1)
  })
})

// ---------------------------------------------------------------------------
// bech32Polymod
// ---------------------------------------------------------------------------

describe('bech32Polymod', () => {
  it('returns 1 for empty input', () => {
    expect(bech32Polymod([])).toBe(1)
  })

  it('returns a number (basic smoke test)', () => {
    const result = bech32Polymod([3, 3, 0, 2, 3, 0, 14, 20, 15, 7, 13])
    expect(typeof result).toBe('number')
    expect(Number.isFinite(result)).toBe(true)
  })
})

// ---------------------------------------------------------------------------
// convertBits
// ---------------------------------------------------------------------------

describe('convertBits', () => {
  it('converts [0] from 8-bit to 5-bit with padding', () => {
    const result = convertBits([0], 8, 5, true)
    expect(result).not.toBeNull()
    expect(result).toEqual([0, 0])
  })

  it('returns null for out-of-range byte', () => {
    expect(convertBits([256], 8, 5, true)).toBeNull()
    expect(convertBits([-1], 8, 5, true)).toBeNull()
  })

  it('round-trips 20-byte witness program (P2WPKH)', () => {
    const prog = Array.from({ length: 20 }, (_, i) => i)
    const encoded = convertBits(prog, 8, 5, true)
    expect(encoded).not.toBeNull()
    const decoded = convertBits(encoded!, 5, 8, false)
    expect(decoded).toEqual(prog)
  })

  it('round-trips 32-byte witness program (P2WSH / P2TR)', () => {
    const prog = Array.from({ length: 32 }, (_, i) => i * 8 % 256)
    const encoded = convertBits(prog, 8, 5, true)
    expect(encoded).not.toBeNull()
    const decoded = convertBits(encoded!, 5, 8, false)
    expect(decoded).toEqual(prog)
  })
})

// ---------------------------------------------------------------------------
// segwitAddress — BIP-173/350 test vectors
// ---------------------------------------------------------------------------

describe('segwitAddress', () => {
  it('returns null for null/empty/short spk', () => {
    expect(segwitAddress('')).toBeNull()
    expect(segwitAddress('0014')).toBeNull() // spk.length < (2+len)*2: len=20 needs 44 hex chars
  })

  it('returns null for unknown witness version', () => {
    // version byte 0x61 is not 0x00 and not 0x51-0x60
    const spk = '61' + '14' + '00'.repeat(20)
    expect(segwitAddress(spk)).toBeNull()
  })

  it('P2WPKH (v0, 20-byte program) — BIP-173 test vector mainnet', () => {
    // scriptPubKey: OP_0 <20-byte-hash>
    // Using the BIP-173 example: bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4
    // That corresponds to witnessProgram = 751e76e8199196d454941c45d1b3a323f1433bd6
    const prog = '751e76e8199196d454941c45d1b3a323f1433bd6'
    const spk = '0014' + prog
    const addr = segwitAddress(spk, 'bc')
    expect(addr).toBe('bc1qw508d6qejxtdg4y5r3zarvary0c5xw7kv8f3t4')
  })

  it('P2WSH (v0, 32-byte program) — BIP-173 test vector mainnet', () => {
    // bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3
    // witnessProgram = 1863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262
    const prog = '1863143c14c5166804bd19203356da136c985678cd4d27a1b8c6329604903262'
    const spk = '0020' + prog
    const addr = segwitAddress(spk, 'bc')
    expect(addr).toBe('bc1qrp33g0q5c5txsp9arysrx4k6zdkfs4nce4xj0gdcccefvpysxf3qccfmv3')
  })

  it('P2TR (v1, 32-byte program) — BIP-350 test vector mainnet', () => {
    // bc1p5cyxnuxmeuwuvkwfem96lqzszd02n6xdcjrs20cac6yqjjwudpxqkedrcr
    // witnessProgram = a60869f0dbcf1dc659c9cecbaf8050135ea9e8cdc487053f1dc6880949dc684c
    const prog = 'a60869f0dbcf1dc659c9cecbaf8050135ea9e8cdc487053f1dc6880949dc684c'
    const spk = '5120' + prog
    const addr = segwitAddress(spk, 'bc')
    expect(addr).toBe('bc1p5cyxnuxmeuwuvkwfem96lqzszd02n6xdcjrs20cac6yqjjwudpxqkedrcr')
  })

  it('returns null for v0 program with wrong length (not 20 or 32)', () => {
    const spk = '0010' + '00'.repeat(16)
    expect(segwitAddress(spk)).toBeNull()
  })

  it('returns null for program length < 2', () => {
    const spk = '0001' + '00'
    expect(segwitAddress(spk)).toBeNull()
  })

  it('uses testnet hrp correctly', () => {
    const prog = '751e76e8199196d454941c45d1b3a323f1433bd6'
    const spk = '0014' + prog
    const addr = segwitAddress(spk, 'tb')
    expect(addr).toBeTruthy()
    expect(addr!.startsWith('tb1')).toBe(true)
  })
})

// ---------------------------------------------------------------------------
// nbitsToDifficulty
// ---------------------------------------------------------------------------

describe('nbitsToDifficulty', () => {
  it('returns 0 for invalid input', () => {
    expect(nbitsToDifficulty('')).toBe(0)
    expect(nbitsToDifficulty('00000000')).toBe(0)
  })

  it('genesis block nbits (0x1d00ffff) → difficulty 1', () => {
    // BTC genesis: nBits = 0x1d00ffff, difficulty = 1.0
    expect(nbitsToDifficulty('1d00ffff')).toBeCloseTo(1.0, 5)
  })

  it('higher target → lower difficulty', () => {
    // 0x1e0fffff has a larger mantissa than 0xffff, so difficulty < 1
    const d = nbitsToDifficulty('1e0fffff')
    expect(d).toBeGreaterThan(0)
    expect(d).toBeLessThan(1)
  })

  it('compressed target → higher difficulty', () => {
    // Smaller exponent means tighter target → higher difficulty
    const d1 = nbitsToDifficulty('1d00ffff') // difficulty 1
    const d2 = nbitsToDifficulty('1c00ffff') // same mantissa, smaller exponent
    expect(d2).toBeGreaterThan(d1)
  })

  it('returns 0 for zero mantissa', () => {
    // mantissa = 0 → division by zero guard
    expect(nbitsToDifficulty('1d000000')).toBe(0)
  })
})

// ---------------------------------------------------------------------------
// coinbaseHeight
// ---------------------------------------------------------------------------

// Synthetic coinbase tx prefix (coinb1) byte layout:
//   version(4) + in_count(1) + prev_hash(32) + prev_idx(4) + scriptSig_len(varint)
//   + height_push_len(1) + height_bytes(N)
//
// Total fixed prefix to scriptSig_len: 4+1+32+4 = 41 bytes
//
// We encode a minimal coinb1 with a block height so coinbaseHeight can parse it.
//
// block height 840000 = 0x0CD2C0
// little-endian 3-byte: [0xC0, 0xD2, 0x0C]
// BIP34 push: 0x03 (push 3 bytes) followed by [0xC0, 0xD2, 0x0C]
//
// coinb1 = version(4) + in_count(1) + prev(32) + prev_idx(4) + scriptSig_len(1)
//        + push_len(1) + height_bytes(3) + ...
// = '01000000' + '01' + '00'.repeat(32) + 'ffffffff'
//   + scriptSig_len + '03' + 'c0d20c'

function makeCoinb1(height: number, extraPayload = ''): string {
  const fixed = '01000000' + '01' + '00'.repeat(32) + 'ffffffff'
  // encode height LE
  const bytes: number[] = []
  let h = height
  while (h > 0) { bytes.push(h & 0xff); h >>= 8 }
  const pushLen = bytes.length.toString(16).padStart(2, '0')
  const heightHex = bytes.map(b => b.toString(16).padStart(2, '0')).join('')
  // scriptSig_len = 1 (push_len byte) + bytes.length + extraPayload bytes
  const extraBytes = extraPayload.length / 2
  const sigLen = (1 + bytes.length + extraBytes).toString(16).padStart(2, '0')
  return fixed + sigLen + pushLen + heightHex + extraPayload
}

describe('coinbaseHeight', () => {
  it('returns null for null/short strings', () => {
    expect(coinbaseHeight(null as unknown as string)).toBeNull()
    expect(coinbaseHeight('')).toBeNull()
    expect(coinbaseHeight('aabb')).toBeNull()
  })

  it('parses block height 1', () => {
    expect(coinbaseHeight(makeCoinb1(1))).toBe(1)
  })

  it('parses block height 840000 (Bitcoin halving block)', () => {
    expect(coinbaseHeight(makeCoinb1(840000))).toBe(840000)
  })

  it('parses block height 0x010203 (multi-byte)', () => {
    // 0x010203 = 66051 decimal, LE bytes: [3, 2, 1]
    expect(coinbaseHeight(makeCoinb1(0x010203))).toBe(0x010203)
  })

  it('returns null for pushLen=0', () => {
    // force a pushLen of 0 by putting 0x00 at the push byte position
    const coinb1 = '01000000' + '01' + '00'.repeat(32) + 'ffffffff' + '01' + '00'
    expect(coinbaseHeight(coinb1)).toBeNull()
  })
})

// ---------------------------------------------------------------------------
// coinbaseTag
// ---------------------------------------------------------------------------

describe('coinbaseTag', () => {
  it('returns null for null/empty coinb1', () => {
    expect(coinbaseTag(null as unknown as string, '')).toBeNull()
    expect(coinbaseTag('', '')).toBeNull()
  })

  it('extracts printable tag after height push', () => {
    // Append an ASCII pool tag "/Pool/" after the height bytes
    const poolTag = '/Pool/'
    const tagHex = Array.from(poolTag).map(c => c.charCodeAt(0).toString(16).padStart(2, '0')).join('')
    const coinb1 = makeCoinb1(840000, tagHex)
    const tag = coinbaseTag(coinb1, '')
    expect(tag).toBeTruthy()
    expect(tag).toContain('Pool')
  })

  it('returns null when no printable run >= 3 chars found', () => {
    // Append non-printable bytes
    const coinb1 = makeCoinb1(1, '010203')
    expect(coinbaseTag(coinb1, '')).toBeNull()
  })
})

// ---------------------------------------------------------------------------
// coinbaseTotalReward
// ---------------------------------------------------------------------------

// Synthetic coinb2 layout:
//   nSequence(4) + out_count(1) + [value_sats(8-le) + scriptLen(1) + script(N)] * N + locktime(4)
//
// We build a minimal coinb2 with one output of known value.

function makeCoinb2(outputs: Array<{ sats: bigint; script: string }>): string {
  // nSequence
  let hex = 'ffffffff'
  hex += outputs.length.toString(16).padStart(2, '0')
  for (const { sats, script } of outputs) {
    // value as 8-byte LE
    let v = sats
    const vbytes: number[] = []
    for (let i = 0; i < 8; i++) {
      vbytes.push(Number(v & 0xffn))
      v >>= 8n
    }
    hex += vbytes.map(b => b.toString(16).padStart(2, '0')).join('')
    hex += (script.length / 2).toString(16).padStart(2, '0')
    hex += script
  }
  return hex
}

describe('coinbaseTotalReward', () => {
  it('returns null for null/empty', () => {
    expect(coinbaseTotalReward(null as unknown as string)).toBeNull()
    expect(coinbaseTotalReward('')).toBeNull()
  })

  it('parses single output reward', () => {
    const sats = 312500000n // 3.125 BTC (post-halving subsidy)
    const cb2 = makeCoinb2([{ sats, script: '001400112233445566778899aabbccddeeff00112233' }])
    expect(coinbaseTotalReward(cb2)).toBe(312500000)
  })

  it('sums multiple outputs', () => {
    const cb2 = makeCoinb2([
      { sats: 312500000n, script: '00140011223344556677889900112233445566778899' },
      { sats: 100000n, script: '0014aabbccddeeff00112233445566778899aabbccdd' },
    ])
    expect(coinbaseTotalReward(cb2)).toBe(312600000)
  })
})

// ---------------------------------------------------------------------------
// coinbasePayoutSpk
// ---------------------------------------------------------------------------

describe('coinbasePayoutSpk', () => {
  it('returns null for null/empty', () => {
    expect(coinbasePayoutSpk(null as unknown as string)).toBeNull()
    expect(coinbasePayoutSpk('')).toBeNull()
  })

  it('extracts first output scriptPubKey hex', () => {
    const script = '0014751e76e8199196d454941c45d1b3a323f1433bd6'
    const cb2 = makeCoinb2([{ sats: 312500000n, script }])
    expect(coinbasePayoutSpk(cb2)).toBe(script)
  })

  it('extracts first output spk even when multiple outputs present', () => {
    const script1 = '0014751e76e8199196d454941c45d1b3a323f1433bd6'
    const script2 = '0014aabbccddeeff00112233445566778899aabbccdd'
    const cb2 = makeCoinb2([
      { sats: 312500000n, script: script1 },
      { sats: 100000n, script: script2 },
    ])
    expect(coinbasePayoutSpk(cb2)).toBe(script1)
  })
})
