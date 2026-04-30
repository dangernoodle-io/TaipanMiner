<script lang="ts">
  import { onMount } from 'svelte'
  import { stats, info, pool } from '../lib/stores'
  import { fetchPool, putPool, switchPool, deletePoolSlot, type PoolConfigInput, type PoolPutBody } from '../lib/api'
  import { fmtRelative } from '../lib/fmt'

  type PoolForm = { host: string; port: number; wallet: string; worker: string; pool_pass: string }

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
  let rebootRequired = false
  let editingIdx: number | null = null  // 0 = primary, 1 = fallback
  let switching = false
  // Frozen snapshot of $pool taken at switch-click; rendered in place of the
  // live store while switching so the page doesn't flicker as the firmware
  // tears down the old session. Cleared once the new session is observed.
  let frozenPool: typeof $pool | null = null
  $: displayPool = switching ? frozenPool : $pool

  let form: PoolForm = { host: '', port: 0, wallet: '', worker: '', pool_pass: '' }
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
        pool_pass: ''
      }
    } else {
      form = { host: '', port: 0, wallet: '', worker: '', pool_pass: '' }
    }
  }

  function cancelEdit() {
    editingIdx = null
    saveMsg = ''
  }

  async function handleSave() {
    if (editingIdx === null) return
    saveMsg = ''
    saving = true
    try {
      const body: PoolPutBody = {
        primary: $pool?.configured?.primary ? {
          host: editingIdx === 0 ? form.host.trim() : $pool.configured?.primary.host,
          port: editingIdx === 0 ? form.port : $pool.configured?.primary.port,
          worker: editingIdx === 0 ? form.worker.trim() : $pool.configured?.primary.worker,
          wallet: editingIdx === 0 ? form.wallet.trim() : $pool.configured?.primary.wallet,
          pool_pass: editingIdx === 0 ? form.pool_pass : ''
        } : {
          host: form.host.trim(),
          port: form.port,
          worker: form.worker.trim(),
          wallet: form.wallet.trim(),
          pool_pass: form.pool_pass
        },
        fallback: editingIdx === 1 ? {
          host: form.host.trim(),
          port: form.port,
          worker: form.worker.trim(),
          wallet: form.wallet.trim(),
          pool_pass: form.pool_pass
        } : ($pool?.configured?.fallback ? {
          host: $pool.configured?.fallback.host,
          port: $pool.configured?.fallback.port,
          worker: $pool.configured?.fallback.worker,
          wallet: $pool.configured?.fallback.wallet,
          pool_pass: ''
        } : null)
      }
      const res = await putPool(body)
      rebootRequired = res.reboot_required
      saveMsg = res.reboot_required ? 'Saved. Reboot required.' : 'Saved.'
      editingIdx = null
      pool.set(await fetchPool())
    } catch (e) {
      saveMsg = `Save failed: ${(e as Error).message}`
    } finally {
      saving = false
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

  function truncWallet(w: string | undefined): string {
    if (!w) return '—'
    if (w.length <= 14) return w
    return `${w.slice(0, 6)}…${w.slice(-4)}`
  }
</script>

<div class="pool-grid" class:is-switching={switching}>
  {#if switching}
    <div class="switching-overlay" role="status" aria-live="polite">
      <div class="spinner" aria-hidden="true"></div>
      <div class="switching-msg">Switching pools…</div>
      <div class="switching-sub">reconnecting stratum</div>
    </div>
  {/if}
  <!-- Active pool status — read-only metrics from /api/pool (TA-281). -->
  <section class="card active">
    <header class="active-head">
      <h3>Active</h3>
      {#if displayPool?.notify && coinbaseTag(displayPool.notify.coinb1, displayPool.notify.coinb2)}
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
        {#if coinbaseHeight(n.coinb1) != null}
          <div class="sf">
            <div class="sk">block height</div>
            <div class="sv mono">{fmtNetDiff(coinbaseHeight(n.coinb1) ?? 0)}</div>
          </div>
        {/if}
        {#if coinbaseTotalReward(n.coinb2) != null}
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
      {#if coinbasePayoutSpk(n.coinb2)}
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
        <!-- Primary -->
        <div class="pool-row" class:editing={editingIdx === 0}>
          {#if editingIdx !== 0}
            <div class="summary">
              {#if displayPool.configured?.primary}
                <div class="info">
                  <div class="caption-row">
                    <span class="kind-caption">Primary</span>
                    {#if displayPool.active_pool_idx === 0 && displayPool.connected}
                      <span class="active-tag">ACTIVE</span>
                    {/if}
                  </div>
                  <div class="endpoint-line">
                    <span class="ep-host">{displayPool.configured?.primary.host}</span>{#if displayPool.configured?.primary.port}<span class="ep-port">:{displayPool.configured?.primary.port}</span>{/if}
                    <span class="meta-sep">·</span>
                    <span class="ep-worker">{displayPool.configured?.primary.worker}</span>
                    <span class="meta-sep">·</span>
                    <span class="ep-wallet mono" title={displayPool.configured?.primary.wallet}>{truncWallet(displayPool.configured?.primary.wallet)}</span>
                  </div>
                  <div class="settings-line">
                    <label class="setting-toggle disabled-ctrl" title="extranonce subscription — pending TA-306">
                      <input type="checkbox" disabled />
                      <span>extranonce.subscribe</span>
                      <span class="pending-tag">TA-306</span>
                    </label>
                    <label class="setting-toggle disabled-ctrl" title="decode coinbase tx — pending TA-307">
                      <input type="checkbox" checked disabled />
                      <span>decode coinbase</span>
                      <span class="pending-tag">TA-307</span>
                    </label>
                  </div>
                </div>
                <div class="actions">
                  {#if displayPool.active_pool_idx === 1 && displayPool.configured?.fallback}
                    <button class="btn outline sm" on:click={() => handleSwitch(0)} disabled={switching}>{switching ? 'Switching…' : 'Switch'}</button>
                  {/if}
                  <button class="btn outline sm" on:click={() => startEdit(0)}>Edit</button>
                  {#if displayPool.configured?.fallback}
                    <button class="btn outline sm danger" on:click={() => handleRemove('primary')} disabled={saving} title="Remove primary; fallback will be promoted">Remove</button>
                  {/if}
                </div>
              {:else}
                <div class="info">
                  <div class="kind-caption">Primary</div>
                  <div class="placeholder">not configured</div>
                </div>
                <div class="actions">
                  <button class="btn outline sm" on:click={() => startEdit(0)}>Configure</button>
                </div>
              {/if}
            </div>
          {:else}
            <form class="edit-form" on:submit|preventDefault={handleSave}>
              <div class="edit-head">
                <span class="kind">Primary</span>
              </div>
              <div class="fields">
                <label>
                  <span class="lbl">Host</span>
                  <input type="text" bind:value={form.host} maxlength="63" required />
                </label>
                <label class="narrow">
                  <span class="lbl">Port</span>
                  <input type="number" bind:value={form.port} min="1" max="65535" required />
                </label>
                <label>
                  <span class="lbl">Worker</span>
                  <input type="text" bind:value={form.worker} placeholder={hostname || $info?.worker_name || 'miner-1'} required />
                </label>
                <label class="wide">
                  <span class="lbl">Wallet</span>
                  <input type="text" bind:value={form.wallet} spellcheck="false" required />
                </label>
                <label>
                  <span class="lbl">Password</span>
                  <input type="text" bind:value={form.pool_pass} placeholder="x" />
                </label>
              </div>
              <div class="actions">
                <button type="submit" class="btn primary sm" disabled={saving}>{saving ? 'Saving…' : 'Save'}</button>
                <button type="button" class="btn outline sm" on:click={cancelEdit} disabled={saving}>Cancel</button>
                {#if saveMsg}<span class="msg" class:warn={rebootRequired}>{saveMsg}</span>{/if}
              </div>
            </form>
          {/if}
        </div>

        <!-- Fallback -->
        <div class="pool-row" class:editing={editingIdx === 1} class:disabled={!displayPool.configured?.fallback && editingIdx !== 1}>
          {#if editingIdx !== 1}
            <div class="summary">
              {#if displayPool.configured?.fallback}
                <div class="info">
                  <div class="caption-row">
                    <span class="kind-caption">Fallback</span>
                    {#if displayPool.active_pool_idx === 1 && displayPool.connected}
                      <span class="active-tag">ACTIVE</span>
                    {/if}
                  </div>
                  <div class="endpoint-line">
                    <span class="ep-host">{displayPool.configured?.fallback.host}</span>{#if displayPool.configured?.fallback.port}<span class="ep-port">:{displayPool.configured?.fallback.port}</span>{/if}
                    <span class="meta-sep">·</span>
                    <span class="ep-worker">{displayPool.configured?.fallback.worker}</span>
                    <span class="meta-sep">·</span>
                    <span class="ep-wallet mono" title={displayPool.configured?.fallback.wallet}>{truncWallet(displayPool.configured?.fallback.wallet)}</span>
                  </div>
                  <div class="settings-line">
                    <label class="setting-toggle disabled-ctrl" title="extranonce subscription — pending TA-306">
                      <input type="checkbox" disabled />
                      <span>extranonce.subscribe</span>
                      <span class="pending-tag">TA-306</span>
                    </label>
                    <label class="setting-toggle disabled-ctrl" title="decode coinbase tx — pending TA-307">
                      <input type="checkbox" checked disabled />
                      <span>decode coinbase</span>
                      <span class="pending-tag">TA-307</span>
                    </label>
                  </div>
                </div>
                <div class="actions">
                  {#if displayPool.active_pool_idx === 0 && displayPool.configured?.primary}
                    <button class="btn outline sm" on:click={() => handleSwitch(1)} disabled={switching}>{switching ? 'Switching…' : 'Switch'}</button>
                  {/if}
                  <button class="btn outline sm" on:click={() => startEdit(1)}>Edit</button>
                  <button class="btn outline sm danger" on:click={() => handleRemove('fallback')} disabled={saving} title="Remove fallback pool">Remove</button>
                </div>
              {:else}
                <div class="info">
                  <div class="kind-caption">Fallback</div>
                  <div class="placeholder">not configured · optional second pool for failover</div>
                </div>
                <div class="actions">
                  <button class="btn outline sm" on:click={() => startEdit(1)}>+ Add</button>
                </div>
              {/if}
            </div>
          {:else}
            <form class="edit-form" on:submit|preventDefault={handleSave}>
              <div class="edit-head">
                <span class="kind">Fallback</span>
              </div>
              <div class="fields">
                <label>
                  <span class="lbl">Host</span>
                  <input type="text" bind:value={form.host} maxlength="63" required />
                </label>
                <label class="narrow">
                  <span class="lbl">Port</span>
                  <input type="number" bind:value={form.port} min="1" max="65535" required />
                </label>
                <label>
                  <span class="lbl">Worker</span>
                  <input type="text" bind:value={form.worker} placeholder={hostname || $info?.worker_name || 'miner-1'} required />
                </label>
                <label class="wide">
                  <span class="lbl">Wallet</span>
                  <input type="text" bind:value={form.wallet} spellcheck="false" required />
                </label>
                <label>
                  <span class="lbl">Password</span>
                  <input type="text" bind:value={form.pool_pass} placeholder="x" />
                </label>
              </div>
              <div class="actions">
                <button type="submit" class="btn primary sm" disabled={saving}>{saving ? 'Saving…' : 'Save'}</button>
                <button type="button" class="btn outline sm" on:click={cancelEdit} disabled={saving}>Cancel</button>
                {#if saveMsg}<span class="msg" class:warn={rebootRequired}>{saveMsg}</span>{/if}
              </div>
            </form>
          {/if}
        </div>
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

  .payout-strip {
    display: flex;
    justify-content: space-between;
    align-items: baseline;
    gap: 12px;
    margin-top: 12px;
    padding-top: 10px;
    border-top: 1px dashed var(--border);
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

  h3 {
    margin: 0;
    font-size: 12px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    font-weight: 600;
  }

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

  .active-tag {
    display: inline-block;
    font-size: 9px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--success);
    background: color-mix(in srgb, var(--success) 12%, transparent);
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

  /* Pool list */
  .pool-list {
    display: flex;
    flex-direction: column;
  }

  .pool-row + .pool-row {
    border-top: 1px dashed var(--border);
  }

  .pool-row.disabled { opacity: 0.5; }
  .pool-row.editing { background: var(--bg); }

  .summary {
    display: flex;
    align-items: center;
    gap: 12px;
    padding: 12px 4px;
    font-size: 12px;
  }

  .summary .info {
    flex: 1 1 auto;
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 2px;
  }

  .summary .caption-row {
    display: flex;
    align-items: center;
    gap: 8px;
    margin-bottom: 2px;
  }

  .summary .caption-row .kind-caption { margin-bottom: 0; }

  .summary .kind-caption {
    color: var(--label);
    text-transform: uppercase;
    letter-spacing: 0.6px;
    font-size: 11px;
    font-weight: 700;
    margin-bottom: 2px;
  }

  .summary .endpoint-line {
    display: flex;
    align-items: baseline;
    gap: 8px;
    color: var(--text);
    font-size: 14px;
    font-weight: 600;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    min-width: 0;
  }

  .summary .endpoint-line .ep-host {
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    min-width: 0;
  }

  .summary .endpoint-line .ep-port {
    color: var(--muted);
    font-weight: 500;
    font-variant-numeric: tabular-nums;
    flex-shrink: 0;
    margin-left: -8px;
  }

  .summary .endpoint-line .ep-worker,
  .summary .endpoint-line .ep-wallet {
    color: var(--muted);
    font-size: 12px;
    font-weight: 500;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    min-width: 0;
  }

  .summary .endpoint-line .meta-sep {
    flex-shrink: 0;
    opacity: 0.4;
    color: var(--muted);
  }

  .summary .settings-line {
    display: flex;
    flex-wrap: wrap;
    gap: 14px;
    margin-top: 6px;
    font-size: 11px;
  }

  .summary .setting-toggle {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    color: var(--label);
    text-transform: uppercase;
    letter-spacing: 0.4px;
    font-size: 10px;
    cursor: not-allowed;
  }

  .summary .setting-toggle.disabled-ctrl { opacity: 0.55; }

  .summary .setting-toggle input[type="checkbox"] {
    width: 12px;
    height: 12px;
    cursor: not-allowed;
  }

  .summary .actions {
    display: flex;
    align-items: center;
    gap: 8px;
    flex-shrink: 0;
  }

  .placeholder {
    color: var(--muted);
    font-style: italic;
    font-size: 11px;
  }

  .btn.danger {
    color: var(--danger);
    border-color: color-mix(in srgb, var(--danger) 50%, transparent);
  }
  .btn.danger:hover { background: color-mix(in srgb, var(--danger) 12%, transparent); }

  .edit-head .kind {
    color: var(--label);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-size: 10px;
    font-weight: 600;
  }

  @media (max-width: 720px) {
    .summary {
      flex-wrap: wrap;
      row-gap: 8px;
    }
    .summary .info {
      flex-basis: 100%;
      order: 3;
    }
    .summary .actions {
      flex-basis: 100%;
      justify-content: flex-end;
      order: 4;
    }
    .summary .endpoint-line { font-size: 13px; }
    .summary .endpoint-line { flex-wrap: wrap; }
  }

  .mono { font-family: ui-monospace, Menlo, monospace; font-size: 11px; }

  /* Edit form (inline) */
  .edit-form {
    padding: 12px 4px;
  }

  .edit-head {
    display: flex;
    align-items: center;
    gap: 12px;
    margin-bottom: 10px;
  }

  .fields {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
    gap: 10px 12px;
    margin-bottom: 10px;
  }

  .fields .narrow { max-width: 120px; }
  .fields .wide { grid-column: 1 / -1; }

  label {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }

  .lbl {
    font-size: 9px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    font-weight: 600;
  }

  input[type="text"], input[type="number"] {
    width: 100%;
    box-sizing: border-box;
    min-width: 0;
    padding: 7px 10px;
    background: var(--input);
    color: var(--text);
    border: 1px solid var(--border);
    border-radius: 4px;
    font-size: 12px;
    font-family: inherit;
    font-variant-numeric: tabular-nums;
    transition: border-color 0.15s;
  }

  input:focus { outline: none; border-color: var(--accent); }
  input:disabled { opacity: 0.5; cursor: not-allowed; }

  .actions {
    display: flex;
    align-items: center;
    gap: 10px;
    flex-wrap: wrap;
  }


  .msg {
    font-size: 11px;
    color: var(--success);
  }

  .msg.warn { color: var(--warning); }
</style>
