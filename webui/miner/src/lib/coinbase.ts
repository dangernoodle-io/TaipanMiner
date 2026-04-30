// Bitcoin coinbase transaction parsing and bech32(m) address encoding.

// ---- bech32 / segwit -------------------------------------------------------

export const BECH32_CHARS = 'qpzry9x8gf2tvdw0s3jn54khce6mua7l'

export function bech32Polymod(values: number[]): number {
  const G = [0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3]
  let chk = 1
  for (const v of values) {
    const top = chk >>> 25
    chk = ((chk & 0x1ffffff) << 5) ^ v
    for (let i = 0; i < 5; i++) if ((top >> i) & 1) chk ^= G[i]
  }
  return chk
}

export function bech32HrpExpand(hrp: string): number[] {
  const out: number[] = []
  for (let i = 0; i < hrp.length; i++) out.push(hrp.charCodeAt(i) >> 5)
  out.push(0)
  for (let i = 0; i < hrp.length; i++) out.push(hrp.charCodeAt(i) & 0x1f)
  return out
}

export function bech32Checksum(hrp: string, data: number[], spec: 'bech32' | 'bech32m'): number[] {
  const constN = spec === 'bech32m' ? 0x2bc830a3 : 1
  const values = bech32HrpExpand(hrp).concat(data, [0, 0, 0, 0, 0, 0])
  const polymod = bech32Polymod(values) ^ constN
  return [0, 1, 2, 3, 4, 5].map(i => (polymod >> (5 * (5 - i))) & 31)
}

export function convertBits(bytes: number[], from: number, to: number, pad: boolean): number[] | null {
  let acc = 0, bits = 0
  const out: number[] = []
  const maxv = (1 << to) - 1
  for (const b of bytes) {
    if (b < 0 || b >> from) return null
    acc = (acc << from) | b
    bits += from
    while (bits >= to) {
      bits -= to
      out.push((acc >> bits) & maxv)
    }
  }
  if (pad && bits) out.push((acc << (to - bits)) & maxv)
  else if (!pad && (bits >= from || ((acc << (to - bits)) & maxv))) return null
  return out
}

// Return bech32(m) address for a witness-program scriptPubKey, or null.
// Supports v0 (P2WPKH/P2WSH) and v1 (P2TR).
export function segwitAddress(spkHex: string, hrp = 'bc'): string | null {
  if (!spkHex || spkHex.length < 4) return null
  const v = parseInt(spkHex.slice(0, 2), 16)
  const len = parseInt(spkHex.slice(2, 4), 16)
  if (spkHex.length !== (2 + len) * 2) return null
  let witver: number
  if (v === 0x00) witver = 0
  else if (v >= 0x51 && v <= 0x60) witver = v - 0x50
  else return null
  if (witver === 0 && len !== 20 && len !== 32) return null
  if (len < 2 || len > 40) return null
  const program: number[] = []
  for (let i = 4; i + 1 < spkHex.length; i += 2) program.push(parseInt(spkHex.slice(i, i + 2), 16))
  const conv = convertBits(program, 8, 5, true)
  if (!conv) return null
  const data = [witver].concat(conv)
  const spec = witver === 0 ? 'bech32' : 'bech32m'
  const checksum = bech32Checksum(hrp, data, spec)
  return hrp + '1' + data.concat(checksum).map(d => BECH32_CHARS[d]).join('')
}

// ---- coinbase parsing ------------------------------------------------------

// nbits is a 4-byte compact representation of the target. Difficulty 1
// corresponds to target 0x00000000FFFF0000…, i.e. nbits 0x1d00ffff. Network
// difficulty = difficulty1_target / target.
export function nbitsToDifficulty(nbits: string): number {
  const word = parseInt(nbits, 16)
  if (!Number.isFinite(word) || word === 0) return 0
  const exp = word >>> 24
  const mantissa = word & 0x007fffff
  if (mantissa === 0) return 0
  // difficulty = (0xffff << ((0x1d - 3) * 8)) / (mantissa << ((exp - 3) * 8))
  //            = (0xffff / mantissa) * 2 ** ((0x1d - exp) * 8)
  return (0xffff / mantissa) * Math.pow(2, (0x1d - exp) * 8)
}

// Extract printable scriptSig tag after the BIP34 height push. Pools usually
// embed their brand here (e.g. "Hashed Max-DGB-Pool", "/ViaBTC/").
export function coinbaseTag(coinb1: string, coinb2: string): string | null {
  if (!coinb1) return null
  let off = 41
  const sigLen = parseInt(coinb1.slice(off * 2, off * 2 + 2), 16)
  if (!Number.isFinite(sigLen) || sigLen >= 0xfd) return null
  off += 1
  const pushLen = parseInt(coinb1.slice(off * 2, off * 2 + 2), 16)
  if (!Number.isFinite(pushLen) || pushLen < 1 || pushLen > 8) return null
  off += 1 + pushLen
  const tail = coinb1.slice(off * 2) + (coinb2 ?? '')
  let txt = ''
  for (let i = 0; i + 1 < tail.length; i += 2) {
    const b = parseInt(tail.slice(i, i + 2), 16)
    if (b >= 0x20 && b <= 0x7e) txt += String.fromCharCode(b)
    else if (txt.length >= 4) break
    else txt = ''
  }
  txt = txt.trim().replace(/^[\/\-\s]+|[\/\-\s]+$/g, '')
  return txt.length >= 3 ? txt : null
}

// BIP34: block height is push-encoded at the start of the coinbase scriptSig.
// coinb1 layout: version(4) + in_count(1) + prev(32) + prev_idx(4) + scriptSig_len(varint) + scriptSig…
export function coinbaseHeight(coinb1: string): number | null {
  if (!coinb1 || coinb1.length < 84) return null
  let off = 41 // bytes of fixed prefix; scriptSig length varint follows
  const lenByte = parseInt(coinb1.slice(off * 2, off * 2 + 2), 16)
  if (!Number.isFinite(lenByte)) return null
  off += 1
  if (lenByte >= 0xfd) return null // longer varints unused for coinbase scriptSig
  const pushLen = parseInt(coinb1.slice(off * 2, off * 2 + 2), 16)
  if (!Number.isFinite(pushLen) || pushLen < 1 || pushLen > 8) return null
  off += 1
  const bytes: number[] = []
  for (let i = 0; i < pushLen; i++) {
    const b = parseInt(coinb1.slice((off + i) * 2, (off + i) * 2 + 2), 16)
    if (!Number.isFinite(b)) return null
    bytes.push(b)
  }
  let h = 0
  for (let i = bytes.length - 1; i >= 0; i--) h = h * 256 + bytes[i]
  return h
}

// Parse coinb2 outputs and return total value in satoshis (subsidy + fees).
export function coinbaseTotalReward(coinb2: string): number | null {
  if (!coinb2) return null
  try {
    const b: number[] = []
    for (let i = 0; i + 1 < coinb2.length; i += 2) b.push(parseInt(coinb2.slice(i, i + 2), 16))
    let off = 4 // nSequence
    const nout = b[off++]
    let total = 0
    for (let k = 0; k < nout; k++) {
      let v = 0
      for (let i = 7; i >= 0; i--) v = v * 256 + b[off + i]
      off += 8
      const sl = b[off++]
      off += sl
      total += v
    }
    return total
  } catch { return null }
}

// Extract first output's scriptPubKey hex from coinb2 (= miner payout).
export function coinbasePayoutSpk(coinb2: string): string | null {
  if (!coinb2) return null
  try {
    let off = 4 + 1 // nSequence + out_count varint (assume <0xfd)
    off += 8 // value
    const sl = parseInt(coinb2.slice(off * 2, off * 2 + 2), 16)
    off += 1
    return coinb2.slice(off * 2, (off + sl) * 2)
  } catch { return null }
}
