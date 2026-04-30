<script lang="ts">
  import { onMount } from 'svelte'
  import { stats, info, pool } from '../lib/stores'
  import { fetchPool, putPool, switchPool, deletePoolSlot, type PoolConfigured, type PoolConfigInput, type PoolPutBody } from '../lib/api'
  import { fmtRelative } from '../lib/fmt'
  import PoolRow from '../components/PoolRow.svelte'

  const POOL_IDXS: (0 | 1)[] = [0, 1]

  type PoolForm = {
    host: string
    port: number
    wallet: string
    worker: string
    pool_pass: string
    extranonce_subscribe: boolean  // TA-306
    decode_coinbase: boolean       // TA-307
  }

  // nbits is a 4-byte compact representation of the target. Difficulty 1
  // corresponds to target 0x00000000FFFF0000…, i.e. nbits 0x1d00ffff. Network
  // difficulty = difficulty1_target / target.
  function nbitsToDifficulty(nbits: string): number {
    const word = parseInt(nbits, 16)
    if (!Number.isFinite(word) || word === 0) return 0
    const exp = word >>> 24
    const mantissa = word & 0x007fffff
    if (mantissa === 0) return 0
    const targetExp = (exp - 3) * 8
    // difficulty = (0xffff << ((0x1d - 3) * 8)) / (mantissa << ((exp - 3) * 8))
    //            = (0xffff / mantissa) * 2 ** ((0x1d - exp) * 8)
    return (0xffff / mantissa) * Math.pow(2, (0x1d - exp) * 8)
  }

  function fmtNetDiff(d: number): string {
    if (!Number.isFinite(d) || d <= 0) return '—'
    if (d >= 1e12) return (d / 1e12).toFixed(2) + 'T'
    if (d >= 1e9)  return (d / 1e9).toFixed(2) + 'G'
    if (d >= 1e6)  return (d / 1e6).toFixed(2) + 'M'
    if (d >= 1e3)  return (d / 1e3).toFixed(2) + 'k'
    return d.toFixed(0)
  }

  // Extract printable scriptSig tag after the BIP34 height push. Pools usually
  // embed their brand here (e.g. "Hashed Max-DGB-Pool", "/ViaBTC/").
  function coinbaseTag(coinb1: string, coinb2: string): string | null {
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

  // Parse coinb2 outputs and return total value in satoshis (subsidy + fees).
  function coinbaseTotalReward(coinb2: string): number | null {
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

  // Return bech32(m) address for a witness-program scriptPubKey, or null.
  // Supports v0 (P2WPKH/P2WSH) and v1 (P2TR).
  const BECH32_CHARS = 'qpzry9x8gf2tvdw0s3jn54khce6mua7l'
  function bech32Polymod(values: number[]): number {
    const G = [0x3b6a57b2, 0x26508e6d, 0x1ea119fa, 0x3d4233dd, 0x2a1462b3]
    let chk = 1
    for (const v of values) {
      const top = chk >>> 25
      chk = ((chk & 0x1ffffff) << 5) ^ v
      for (let i = 0; i < 5; i++) if ((top >> i) & 1) chk ^= G[i]
    }
    return chk
  }
  function bech32HrpExpand(hrp: string): number[] {
    const out: number[] = []
    for (let i = 0; i < hrp.length; i++) out.push(hrp.charCodeAt(i) >> 5)
    out.push(0)
    for (let i = 0; i < hrp.length; i++) out.push(hrp.charCodeAt(i) & 0x1f)
    return out
  }
  function bech32Checksum(hrp: string, data: number[], spec: 'bech32' | 'bech32m'): number[] {
    const constN = spec === 'bech32m' ? 0x2bc830a3 : 1
    const values = bech32HrpExpand(hrp).concat(data, [0, 0, 0, 0, 0, 0])
    const polymod = bech32Polymod(values) ^ constN
    return [0, 1, 2, 3, 4, 5].map(i => (polymod >> (5 * (5 - i))) & 31)
  }
  function convertBits(bytes: number[], from: number, to: number, pad: boolean): number[] | null {
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
  function segwitAddress(spkHex: string, hrp = 'bc'): string | null {
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

  // Extract first output's scriptPubKey hex from coinb2 (= miner payout).
  function coinbasePayoutSpk(coinb2: string): string | null {
    if (!coinb2) return null
    try {
      let off = 4 + 1 // nSequence + out_count varint (assume <0xfd)
      off += 8 // value
      const sl = parseInt(coinb2.slice(off * 2, off * 2 + 2), 16)
      off += 1
      return coinb2.slice(off * 2, (off + sl) * 2)
    } catch { return null }
  }

  function fmtBtc(sats: number): string {
    return (sats / 1e8).toFixed(4) + ' BTC'
  }

  function fmtNtimeAge(ntimeHex: string): string | null {
    const t = parseInt(ntimeHex, 16)
    if (!Number.isFinite(t) || t <= 0) return null
    const ago = Math.floor(Date.now() / 1000) - t
    if (ago < 0) return 'now'
    if (ago < 60) return `${ago}s ago`
    if (ago < 3600) return `${Math.floor(ago / 60)}m ago`
    return `${Math.floor(ago / 3600)}h ago`
  }

  function truncAddr(a: string): string {
    if (!a) return '—'
    if (a.length <= 16) return a
    return `${a.slice(0, 8)}…${a.slice(-6)}`
  }

  // BIP34: block height is push-encoded at the start of the coinbase scriptSig.
  // coinb1 layout: version(4) + in_count(1) + prev(32) + prev_idx(4) + scriptSig_len(varint) + scriptSig…
  function coinbaseHeight(coinb1: string): number | null {
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


  let saving = false
  let saveMsg = ''
  let editingIdx: number | null = null  // 0 = primary, 1 = fallback
  let switching = false
  // Same overlay machinery as switching, but flipped on after a save that
  // forces a fresh stratum session (i.e. saving the active slot).
  let reconnecting = false
  // Frozen snapshot of $pool taken at switch-click; rendered in place of the
  // live store while switching so the page doesn't flicker as the firmware
  // tears down the old session. Cleared once the new session is observed.
  let frozenPool: typeof $pool | null = null
  $: displayPool = (switching || reconnecting) ? frozenPool : $pool

  /* TA-307: per-pool flag for the active session controls UI coinbase
   * decoding. Defaults to true when no active pool / no config so the
   * tiles render normally pre-connect. */
  $: activeDecodeCoinbase = (() => {
    const idx = displayPool?.active_pool_idx
    if (idx === 0) return displayPool?.configured?.primary?.decode_coinbase ?? true
    if (idx === 1) return displayPool?.configured?.fallback?.decode_coinbase ?? true
    return true
  })()

  /* Parse-failed signal: flag is on, notify is non-empty, but no parser
   * recognized any coinbase field. Used to badge the toggle so the user
   * knows the tiles vanished because the parser couldn't read this pool's
   * shape, not because they turned the flag off. */
  $: coinbaseParseFailed = (() => {
    if (!activeDecodeCoinbase) return false
    const n = displayPool?.notify
    if (!n || !n.coinb1 || n.coinb1.length < 84) return false
    return coinbaseHeight(n.coinb1) == null
        && coinbaseTotalReward(n.coinb2) == null
        && coinbasePayoutSpk(n.coinb2) == null
  })()

  let form: PoolForm = { host: '', port: 0, wallet: '', worker: '', pool_pass: '', extranonce_subscribe: false, decode_coinbase: true }
  let autoRotate = false
  let hostname = ''

  onMount(() => {
    // No-op; pool store auto-polls via stores.ts
  })

  function startEdit(idx: number) {
    editingIdx = idx
    saveMsg = ''
    const cfg = idx === 0 ? $pool?.configured?.primary : $pool?.configured?.fallback
    if (cfg) {
      form = {
        host: cfg.host,
        port: cfg.port,
        wallet: cfg.wallet,
        worker: cfg.worker,
        pool_pass: '',
        extranonce_subscribe: cfg.extranonce_subscribe ?? false,
        decode_coinbase: cfg.decode_coinbase ?? true,
      }
    } else {
      form = { host: '', port: 0, wallet: '', worker: '', pool_pass: '', extranonce_subscribe: false, decode_coinbase: true }
    }
  }

  function cancelEdit() {
    editingIdx = null
    saveMsg = ''
  }

  // Build a PUT slot for a non-edited pool by mirroring its current state.
  // Omits pool_pass — firmware preserves it on missing key (see PR #280).
  function slotFromCurrent(c: NonNullable<PoolConfigured>): PoolConfigInput {
    const next: PoolConfigInput = {
      host: c.host,
      port: c.port,
      worker: c.worker,
      wallet: c.wallet,
      pool_pass: '',
      extranonce_subscribe: c.extranonce_subscribe ?? false,
      decode_coinbase: c.decode_coinbase ?? true,
    }
    delete (next as Partial<PoolConfigInput>).pool_pass
    return next
  }

  // Build a PUT slot from the form (the edited pool).
  function slotFromForm(): PoolConfigInput {
    return {
      host: form.host.trim(),
      port: form.port,
      worker: form.worker.trim(),
      wallet: form.wallet.trim(),
      pool_pass: form.pool_pass,
      extranonce_subscribe: form.extranonce_subscribe,
      decode_coinbase: form.decode_coinbase,
    }
  }

  async function handleSave() {
    if (editingIdx === null) return
    saveMsg = ''
    saving = true
    /* If editing the currently-active slot, the save needs to drive a
     * fresh stratum session before the user-visible state lines up with
     * what they just entered. Mirror handleSwitch's freeze-and-poll so
     * the page doesn't flicker with stale values during the reconnect. */
    const editingActive = $pool?.active_pool_idx === editingIdx && $pool?.connected
    const preAge = editingActive ? ($pool?.session_start_ago_s ?? null) : null
    if (editingActive) {
      frozenPool = $pool
      reconnecting = true
    }
    try {
      const cfg = $pool?.configured
      const edited = slotFromForm()
      const body: PoolPutBody = {
        primary: editingIdx === 0
          ? edited
          : (cfg?.primary ? slotFromCurrent(cfg.primary) : edited),
        fallback: editingIdx === 1
          ? edited
          : (cfg?.fallback ? slotFromCurrent(cfg.fallback) : null),
      }
      await putPool(body)
      saveMsg = 'Saved.'
      editingIdx = null

      if (editingActive) {
        /* Wait for the firmware to bring up a fresh session under the new
         * config. Same shape as handleSwitch — bounded poll, fresh-session
         * detector via session_start_ago_s shrinking. */
        const deadline = Date.now() + 15000
        while (Date.now() < deadline) {
          await new Promise(r => setTimeout(r, 750))
          const p = await fetchPool()
          pool.set(p)
          if (!p.connected) continue
          if (p.session_start_ago_s == null) continue
          if (preAge == null || p.session_start_ago_s < preAge) break
        }
      } else {
        pool.set(await fetchPool())
      }
    } catch (e) {
      saveMsg = `Save failed: ${(e as Error).message}`
    } finally {
      saving = false
      reconnecting = false
      frozenPool = null
    }
  }

  async function handleRemove(slot: 'primary' | 'fallback') {
    const isPromote = slot === 'primary'
    const msg = isPromote
      ? 'Remove the primary pool? The fallback will be promoted to primary.'
      : 'Remove the fallback pool? Auto-failover will be disabled.'
    if (!confirm(msg)) return
    saveMsg = ''
    saving = true
    // Freeze the view while the firmware reshuffles slots, similar to switch.
    frozenPool = $pool
    try {
      await deletePoolSlot(slot)
      pool.set(await fetchPool())
    } catch (e) {
      saveMsg = `Remove failed: ${(e as Error).message}`
    } finally {
      saving = false
      frozenPool = null
    }
  }

  async function handleSwitch(idx: 0 | 1) {
    // Freeze the displayed pool BEFORE flipping `switching` so the first
    // reactive tick already sees a stable view.
    frozenPool = $pool
    // Snapshot pre-switch session age. The firmware flips active_pool_idx
    // synchronously and only tears down the stratum socket on the next loop
    // iteration, so checking idx+connected post-call returns true instantly
    // while the *old* session is still up. Wait for a fresh session
    // (new session_start_ago_s smaller than the pre-switch value).
    const preAge = $pool?.session_start_ago_s ?? null
    switching = true
    try {
      await switchPool(idx)
      const deadline = Date.now() + 15000
      while (Date.now() < deadline) {
        await new Promise(r => setTimeout(r, 750))
        const p = await fetchPool()
        pool.set(p)
        if (p.active_pool_idx !== idx) continue
        if (!p.connected) continue
        if (p.session_start_ago_s == null) continue
        if (preAge == null || p.session_start_ago_s < preAge) break
      }
    } catch (e) {
      saveMsg = `Switch failed: ${(e as Error).message}`
    } finally {
      switching = false
      frozenPool = null
    }
  }

</script>

<div class="pool-grid" class:is-switching={switching || reconnecting}>
  {#if switching || reconnecting}
    <div class="switching-overlay" role="status" aria-live="polite">
      <div class="spinner" aria-hidden="true"></div>
      <div class="switching-msg">{reconnecting ? 'Reconnecting…' : 'Switching pools…'}</div>
      <div class="switching-sub">{reconnecting ? 'applying changes' : 'reconnecting stratum'}</div>
    </div>
  {/if}
  <!-- Active pool status — read-only metrics from /api/pool (TA-281). -->
  <section class="card active">
    <header class="active-head">
      <h3>Active</h3>
      {#if activeDecodeCoinbase && displayPool?.notify && coinbaseTag(displayPool.notify.coinb1, displayPool.notify.coinb2)}
        <span class="pool-tag has-tip">
          <span class="tag-prefix">scriptSig</span>
          {coinbaseTag(displayPool.notify.coinb1, displayPool.notify.coinb2)}
          <span class="tip" role="tooltip">
            Block template upstream, read from the coinbase scriptSig. Not necessarily the stratum endpoint you're connected to — proxies and relays often forward another pool's template.
          </span>
        </span>
      {/if}
    </header>
    <div class="status-row">
      <div class="who">
        <div class="host">
          <span class="dot" class:connected={displayPool?.connected === true}
                            class:disconnected={displayPool?.connected === false}
                            class:unknown={displayPool == null}
                aria-hidden="true"></span>
          {displayPool?.host ?? '—'}:{displayPool?.port ?? '—'}
        </div>
        <div class="sub">
          worker {displayPool?.worker ?? '—'}
        </div>
      </div>
      <div class="metrics">
        <div class="m">
          <div class="v">{displayPool?.current_difficulty ?? '—'}</div>
          <div class="k">diff</div>
        </div>
        <div class="m">
          <div class="v">{displayPool?.session_start_ago_s != null ? fmtRelative(displayPool.session_start_ago_s) : '—'}</div>
          <div class="k">session</div>
        </div>
        <div class="m">
          <div class="v">{displayPool?.latency_ms != null ? `${displayPool.latency_ms} ms` : '—'}</div>
          <div class="k">latency</div>
        </div>
      </div>
    </div>
    {#if displayPool?.extranonce1 || displayPool?.version_mask}
      <div class="session-strip">
        {#if displayPool?.extranonce1}
          <div class="sf">
            <div class="sk">extranonce1</div>
            <div class="sv mono" title="server-assigned per-session nonce prefix">
              {displayPool.extranonce1}{displayPool.extranonce2_size != null ? ` · ${displayPool.extranonce2_size}B en2` : ''}
            </div>
          </div>
        {/if}
        {#if displayPool?.version_mask}
          <div class="sf">
            <div class="sk">version mask</div>
            <div class="sv mono" title="BIP-320 version-rolling bits">0x{displayPool.version_mask}</div>
          </div>
        {/if}
      </div>
    {/if}
  </section>

  <!-- Stratum notify preview (TA-288). -->
  {#if displayPool?.notify}
    {@const n = displayPool.notify}
    <section class="card stratum">
      <header class="stratum-head">
        <h3>Current Job</h3>
        <span class="job-id" title="job_id">#{n.job_id}</span>
        {#if n.clean_jobs}
          <span class="clean" title="clean_jobs flag set in mining.notify">CLEAN</span>
        {/if}
      </header>
      <div class="stratum-grid">
        {#if activeDecodeCoinbase && coinbaseHeight(n.coinb1) != null}
          <div class="sf">
            <div class="sk">block height</div>
            <div class="sv mono">{fmtNetDiff(coinbaseHeight(n.coinb1) ?? 0)}</div>
          </div>
        {/if}
        {#if activeDecodeCoinbase && coinbaseTotalReward(n.coinb2) != null}
          <div class="sf">
            <div class="sk">block reward</div>
            <div class="sv mono">{fmtBtc(coinbaseTotalReward(n.coinb2) ?? 0)}</div>
          </div>
        {/if}
        <div class="sf">
          <div class="sk">merkle depth</div>
          <div class="sv mono">{n.merkle_branches.length}</div>
        </div>
        <div class="sf">
          <div class="sk">network diff</div>
          <div class="sv mono">{fmtNetDiff(nbitsToDifficulty(n.nbits))}</div>
        </div>
        <div class="sf">
          <div class="sk">prev block</div>
          <div class="sv mono" title={n.prev_hash}>
            {n.prev_hash.slice(0, 8)}…{n.prev_hash.slice(-8)}
          </div>
        </div>
        {#if fmtNtimeAge(n.ntime)}
          <div class="sf">
            <div class="sk">template age</div>
            <div class="sv mono">{fmtNtimeAge(n.ntime)}</div>
          </div>
        {/if}
        <div class="sf">
          <div class="sk">version</div>
          <div class="sv mono">0x{n.version}</div>
        </div>
      </div>
      {#if activeDecodeCoinbase && coinbasePayoutSpk(n.coinb2)}
        {@const spk = coinbasePayoutSpk(n.coinb2)}
        {@const addr = spk ? segwitAddress(spk) : null}
        <div class="payout-strip">
          <span class="sk">payout</span>
          <span class="sv mono" title={addr ?? spk ?? ''}>
            {addr ?? 'non-segwit ' + (spk ?? '')}
          </span>
        </div>
      {/if}
    </section>
  {/if}

  <!-- Pool rows -->
  <section class="card pools">
    <header class="pools-head">
      <h3>Pools</h3>
      <label class="rotate" class:disabled-ctrl={true}>
        <input type="checkbox" bind:checked={autoRotate} disabled />
        <span>Auto rotate</span>
        <span class="pending-tag">TA-203</span>
      </label>
    </header>

    {#if displayPool}
      <div class="pool-list">
        {#each (POOL_IDXS) as idx (idx)}
          <PoolRow
            idx={idx}
            displayPool={displayPool}
            editing={editingIdx === idx}
            bind:form
            {saving}
            {saveMsg}
            {switching}
            workerPlaceholder={hostname || $info?.worker_name || 'miner-1'}
            on:edit={() => startEdit(idx)}
            on:cancel-edit={cancelEdit}
            on:save={handleSave}
            on:switch={() => handleSwitch(idx)}
            on:remove={() => handleRemove(idx === 0 ? 'primary' : 'fallback')}
          />
        {/each}
      </div>
    {/if}
  </section>
</div>

<style>
  .pool-grid {
    display: grid;
    grid-template-columns: 1fr;
    gap: 14px;
    position: relative;
  }

  .pool-grid.is-switching > :global(section.card) {
    filter: blur(1px);
    pointer-events: none;
    user-select: none;
  }

  .switching-overlay {
    position: absolute;
    inset: 0;
    z-index: 10;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
    gap: 8px;
    background: color-mix(in srgb, var(--bg) 70%, transparent);
    backdrop-filter: blur(2px);
    border-radius: 6px;
  }

  .spinner {
    width: 28px;
    height: 28px;
    border-radius: 50%;
    border: 3px solid color-mix(in srgb, var(--accent) 25%, transparent);
    border-top-color: var(--accent);
    animation: spin 0.8s linear infinite;
  }

  @keyframes spin { to { transform: rotate(360deg); } }

  .switching-msg {
    font-size: 13px;
    font-weight: 600;
    color: var(--text);
  }

  .switching-sub {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--muted);
  }

  .card.pending { opacity: 0.75; }

  .stratum-head {
    display: flex;
    align-items: center;
    gap: 10px;
    margin-bottom: 10px;
  }

  .active-head {
    display: flex;
    align-items: baseline;
    gap: 10px;
    margin-bottom: 10px;
  }


  .pool-tag.has-tip {
    position: relative;
    cursor: help;
    overflow: visible;
    max-width: none;
  }

  .has-tip .tip {
    position: absolute;
    top: calc(100% + 6px);
    left: 0;
    z-index: 50;
    width: max-content;
    max-width: 280px;
    padding: 8px 10px;
    border-radius: 6px;
    background: var(--bg-elevated, #1f1f1f);
    color: var(--text);
    border: 1px solid var(--border);
    box-shadow: 0 6px 16px rgba(0, 0, 0, 0.35);
    font-size: 11px;
    font-weight: 400;
    line-height: 1.45;
    text-transform: none;
    letter-spacing: 0;
    white-space: normal;
    opacity: 0;
    pointer-events: none;
    transform: translateY(-2px);
    transition: opacity 80ms ease-out, transform 80ms ease-out;
  }

  .has-tip:hover .tip,
  .has-tip:focus-within .tip {
    opacity: 1;
    transform: translateY(0);
    transition-delay: 300ms;
  }

  .pool-tag .tag-prefix {
    font-size: 9px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    font-weight: 700;
    margin-right: 6px;
    opacity: 0.85;
  }

  .pool-tag {
    display: inline-block;
    font-size: 11px;
    font-weight: 600;
    color: var(--text);
    padding: 2px 8px;
    border-radius: 4px;
    background: color-mix(in srgb, var(--accent) 18%, transparent);
    border: 1px solid color-mix(in srgb, var(--accent) 45%, transparent);
    max-width: 220px;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    vertical-align: middle;
    margin-left: 6px;
  }

  .stratum-head .job-id {
    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
    font-size: 12px;
    color: var(--accent);
    font-weight: 600;
  }

  .stratum-head .clean {
    font-size: 9px;
    font-weight: 700;
    letter-spacing: 0.5px;
    padding: 2px 6px;
    border-radius: 4px;
    color: var(--success);
    border: 1px solid color-mix(in srgb, var(--success) 50%, transparent);
    background: color-mix(in srgb, var(--success) 12%, transparent);
  }

  .payout-strip,
  .session-strip {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    gap: 18px;
    margin-top: 12px;
    padding-top: 10px;
    border-top: 1px dashed var(--border);
  }

  .session-strip .sf { min-width: 0; }
  .session-strip .sf:last-child { text-align: right; }
  .session-strip .sk {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    margin-bottom: 2px;
  }
  .session-strip .sv {
    font-size: 12px;
    color: var(--text);
    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .payout-strip .sk {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
  }

  .payout-strip .sv {
    font-size: 12px;
    color: var(--text);
    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    min-width: 0;
  }

  @media (max-width: 720px) {
    .session-strip {
      flex-direction: column;
      align-items: stretch;
      gap: 8px;
    }
    .session-strip .sf:last-child { text-align: left; }
    .payout-strip {
      flex-direction: column;
      align-items: stretch;
      gap: 4px;
    }
  }

  .stratum-grid {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    gap: 18px;
    flex-wrap: wrap;
  }

  .stratum-grid .sf { text-align: left; }
  .stratum-grid .sf:last-child { text-align: right; }

  @media (max-width: 720px) {
    .stratum-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
      gap: 10px 18px;
    }
    .stratum-grid .sf,
    .stratum-grid .sf:last-child { text-align: left; }
  }

  .sf .sk {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    margin-bottom: 2px;
    text-align: inherit;
  }

  .sf .sv {
    font-size: 13px;
    color: var(--text);
    font-variant-numeric: tabular-nums;
    text-align: inherit;
  }

  .sf .sv.mono {
    font-family: ui-monospace, SFMono-Regular, Menlo, monospace;
  }

  /* card h3 typography lives in ui-kit utilities.css; this page's h3s sit
     inside flex headers that handle their own spacing, so margin: 0 from
     the shared rule is correct as-is. */

  .pending-tag {
    display: inline-block;
    font-size: 9px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--warning);
    background: rgba(243, 156, 18, 0.12);
    padding: 1px 6px;
    border-radius: 3px;
    font-variant-numeric: tabular-nums;
  }

  .loading, .error { font-size: 12px; color: var(--muted); }
  .error { color: var(--danger); }

  /* Active card */
  .active h3 { margin-bottom: 12px; }

  .status-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 24px;
    flex-wrap: wrap;
  }

  .who .host {
    font-size: 18px;
    font-weight: 600;
    color: var(--text);
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .dot {
    display: inline-block;
    width: 8px;
    height: 8px;
    border-radius: 50%;
    flex-shrink: 0;
    background: var(--muted);
  }
  .dot.connected { background: var(--success); }
  .dot.disconnected { background: var(--danger); }
  .dot.unknown { background: var(--muted); }

  .who .sub {
    font-size: 11px;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    margin-top: 4px;
  }

  .metrics { display: flex; gap: 24px; }

  .m .v {
    font-size: 16px;
    font-weight: 600;
    color: var(--text);
    font-variant-numeric: tabular-nums;
    line-height: 1.1;
  }

  .m .k {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    margin-top: 2px;
  }


  /* Hostname strip */
  .hostname { padding: 10px 16px; }

  .host-row {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
  }

  .host-row .k {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--muted);
  }

  .host-row .v {
    font-size: 13px;
    color: var(--text);
    font-family: ui-monospace, Menlo, monospace;
  }

  /* Pools card header */
  .pools-head {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 12px;
    margin-bottom: 14px;
  }

  .rotate {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    cursor: not-allowed;
  }

  .rotate.disabled-ctrl { opacity: 0.6; }

  /* Pool list — rows + edit form live in components/PoolRow + PoolEditForm. */
  .pool-list {
    display: flex;
    flex-direction: column;
  }

  .mono { font-family: ui-monospace, Menlo, monospace; font-size: 11px; }
</style>
