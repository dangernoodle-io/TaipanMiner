<script lang="ts">
  import { stats, info, pool } from '../lib/stores'
  import { fmtRelative, fmtNetDiff, fmtBtc, fmtNtimeAge, truncAddr, fmtPoolDiff } from '../lib/fmt'
  import { nbitsToDifficulty, coinbaseTag, coinbaseHeight, coinbaseTotalReward, coinbasePayoutSpk, segwitAddress } from '../lib/coinbase'
  import PoolRow from '../components/PoolRow.svelte'
  import PoolEditDialog from '../components/PoolEditDialog.svelte'
  import ConfirmDialog from '../components/ConfirmDialog.svelte'
  import ModalSpinner from '../components/ModalSpinner.svelte'
  import Tooltip from '../components/Tooltip.svelte'
  import RollingRates from '../components/RollingRates.svelte'
  import { createPoolState } from '../lib/poolState.svelte'
  import PoolStatsCard from '../components/PoolStatsCard.svelte'
  import StatField from 'ui-kit/StatField.svelte'
  import InfoRow from 'ui-kit/InfoRow.svelte'

  const POOL_IDXS: (0 | 1)[] = [0, 1]

  const ps = createPoolState()

  let autoRotate = $state(false)
  let hostname = $state('')

  const displayPool = $derived((ps.switching || ps.reconnecting) ? ps.frozenPool : $pool)

  /* TA-307: per-pool flag for the active session controls UI coinbase
   * decoding. Defaults to true when no active pool / no config so the
   * tiles render normally pre-connect. */
  const activeDecodeCoinbase = $derived.by(() => {
    const idx = displayPool?.active_pool_idx
    if (idx === 0) return displayPool?.configured?.primary?.decode_coinbase ?? true
    if (idx === 1) return displayPool?.configured?.fallback?.decode_coinbase ?? true
    return true
  })

  /* Parse-failed signal: flag is on, notify is non-empty, but no parser
   * recognized any coinbase field. Used to badge the toggle so the user
   * knows the tiles vanished because the parser couldn't read this pool's
   * shape, not because they turned the flag off. */
  const coinbaseParseFailed = $derived.by(() => {
    if (!activeDecodeCoinbase) return false
    const n = displayPool?.notify
    if (!n || !n.coinb1 || n.coinb1.length < 84) return false
    return coinbaseHeight(n.coinb1) == null
        && coinbaseTotalReward(n.coinb2) == null
        && coinbasePayoutSpk(n.coinb2) == null
  })
</script>

<div class="pool-grid" class:is-switching={ps.switching || ps.reconnecting}>
  <ModalSpinner
    visible={ps.switching || ps.reconnecting}
    label={ps.reconnecting ? 'Reconnecting…' : 'Switching pools…'}
    sublabel={ps.reconnecting ? 'applying changes' : 'reconnecting stratum'}
  />
  <!-- Active pool status — read-only metrics from /api/pool (TA-281). -->
  <section class="card active">
    <header class="active-head">
      <h3>Active</h3>
      {#if activeDecodeCoinbase && displayPool?.notify && coinbaseTag(displayPool.notify.coinb1, displayPool.notify.coinb2)}
        <Tooltip text="Block template upstream, read from the coinbase scriptSig. Not necessarily the stratum endpoint you're connected to — proxies and relays often forward another pool's template.">
          <span class="pool-tag">
            <span class="tag-prefix">scriptSig</span>
            {coinbaseTag(displayPool.notify.coinb1, displayPool.notify.coinb2)}
          </span>
        </Tooltip>
      {/if}
    </header>
    <div class="status-row">
      <div class="who">
        <div class="host">
          <span class="conn-dot" class:connected={displayPool?.connected === true}
                               class:disconnected={displayPool?.connected === false}
                               class:unknown={displayPool == null}
                aria-hidden="true"></span>
          {displayPool?.host ?? '—'}:{displayPool?.port ?? '—'}
        </div>
        <div class="sub">
          worker {displayPool?.worker ?? '—'}
        </div>
      </div>
      <div class="rolling-eff-wrap">
        <div class="rolling-eff-label">pool-effective</div>
        <RollingRates
          ghs1m={displayPool?.pool_effective_hashrate_1m != null ? displayPool.pool_effective_hashrate_1m / 1e9 : null}
          ghs10m={displayPool?.pool_effective_hashrate_10m != null ? displayPool.pool_effective_hashrate_10m / 1e9 : null}
          ghs1h={displayPool?.pool_effective_hashrate_1h != null ? displayPool.pool_effective_hashrate_1h / 1e9 : null}
          showErr={false}
        />
      </div>
    </div>
    <div class="card-footer">
      <StatField label="diff" mono>{fmtPoolDiff(displayPool?.current_difficulty)}</StatField>
      {#if displayPool?.extranonce1}
        <StatField label="extranonce1" mono title="server-assigned per-session nonce prefix">
          {displayPool.extranonce1}{displayPool.extranonce2_size != null ? ` · ${displayPool.extranonce2_size}B en2` : ''}
        </StatField>
      {/if}
      <StatField label="latency" mono>{displayPool?.latency_ms != null ? `${displayPool.latency_ms} ms` : '—'}</StatField>
      <StatField label="session" mono>{displayPool?.session_start_ago_s != null ? fmtRelative(displayPool.session_start_ago_s) : '—'}</StatField>
      {#if displayPool?.version_mask}
        <StatField label="version mask" mono title="BIP-320 version-rolling bits">0x{displayPool.version_mask}</StatField>
      {/if}
    </div>

    {#if $pool?.stats && $pool.stats.length > 0}
      <details class="disclosure pool-history-expander">
        <summary>
          <h2>Pool History</h2>
          <span class="sum-meta">{$pool.stats.length}</span>
        </summary>
        <div class="stats-list">
          {#each $pool.stats as stat (stat.host + ':' + stat.port)}
            <PoolStatsCard
              {stat}
              active={stat.host === $pool.host && stat.port === $pool.port}
            />
          {/each}
        </div>
      </details>
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
          <StatField label="block height" mono>{fmtNetDiff(coinbaseHeight(n.coinb1) ?? 0)}</StatField>
        {/if}
        {#if activeDecodeCoinbase && coinbaseTotalReward(n.coinb2) != null}
          <StatField label="block reward" mono>{fmtBtc(coinbaseTotalReward(n.coinb2) ?? 0)}</StatField>
        {/if}
        <StatField label="merkle depth" mono>{n.merkle_branches.length}</StatField>
        <StatField label="network diff" mono>{fmtNetDiff(nbitsToDifficulty(n.nbits))}</StatField>
        <StatField label="prev block" mono title={n.prev_hash}>{n.prev_hash.slice(0, 8)}…{n.prev_hash.slice(-8)}</StatField>
        {#if fmtNtimeAge(n.ntime)}
          <StatField label="template age" mono>{fmtNtimeAge(n.ntime)}</StatField>
        {/if}
        <StatField label="version" mono>0x{n.version}</StatField>
      </div>
      {#if activeDecodeCoinbase && coinbasePayoutSpk(n.coinb2)}
        {@const spk = coinbasePayoutSpk(n.coinb2)}
        {@const addr = spk ? segwitAddress(spk) : null}
        {@const payoutMismatch = !!displayPool.wallet && (addr == null || addr !== displayPool.wallet)}
        <dl class="payout-row">
          <InfoRow label="payout" mono bad={payoutMismatch}
          >{addr ?? 'non-segwit ' + (spk ?? '')}{#if payoutMismatch}&nbsp;<Tooltip
              text={`Coinbase pays a different address than your configured wallet. Normal for shared/proxy pools (they pay the pool and credit your share off-chain) — but if you're solo mining your rewards are going elsewhere: a misconfigured pool or a scam. Verify the payout address.`}
              icon placement="top" align="right" />{/if}</InfoRow>
        </dl>
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
        <span class="tag pending">TA-203</span>
      </label>
    </header>

    {#if displayPool}
      <div class="pool-list">
        {#each (POOL_IDXS) as idx (idx)}
          <PoolRow
            idx={idx}
            displayPool={displayPool}
            saving={ps.saving}
            switching={ps.switching}
            onedit={() => ps.startEdit(idx)}
            onswitch={() => ps.handleSwitch(idx)}
            onremove={() => ps.requestRemove(idx === 0 ? 'primary' : 'fallback')}
          />
        {/each}
      </div>
    {/if}
  </section>

</div>

<PoolEditDialog
  open={ps.editingIdx !== null}
  bind:host={ps.formHost}
  bind:port={ps.formPort}
  bind:wallet={ps.formWallet}
  bind:worker={ps.formWorker}
  bind:pool_pass={ps.formPoolPass}
  bind:extranonce_subscribe={ps.formExtranonceSubscribe}
  bind:decode_coinbase={ps.formDecodeCoinbase}
  kind={ps.editingIdx === 0 ? 'Primary' : 'Fallback'}
  saving={ps.saving}
  saveMsg={ps.saveMsg}
  workerPlaceholder={hostname || $info?.mining?.worker_name || 'miner-1'}
  onsave={ps.handleSave}
  oncancel={ps.cancelEdit}
/>

<ConfirmDialog
  open={ps.removeConfirmOpen}
  title="Remove pool?"
  message={ps.removeConfirmMsg}
  confirmLabel="Remove"
  danger
  onconfirm={() => { ps.removeConfirmOpen = false; ps.doRemove() }}
  oncancel={() => { ps.removeConfirmOpen = false; ps.pendingRemoveSlot = null }}
/>

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
    flex-wrap: wrap;
  }


  /* the Tooltip wrapper is inline-flex (content-sized); let it shrink + cap to
     the header width so the pill inside actually wraps instead of overflowing. */
  .active-head :global(.has-tip) {
    min-width: 0;
    max-width: 100%;
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
    max-width: 100%;
    min-width: 0;
    overflow-wrap: anywhere;
    word-break: break-word;
    white-space: normal;
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

  /* session / payout strip: stat-field children need min-width guard */
  .card-footer :global(.stat-field) { min-width: 0; }
  .card-footer :global(.stat-field:last-child) { text-align: right; }

  @media (max-width: 720px) {
    .card-footer :global(.stat-field:last-child) { text-align: left; }
  }

  /* payout: a single label-left / value-right row (InfoRow) — the address
     right-aligns in the value column, matching the original Pool layout.
     Keeps the .card-footer dashed divider above it. */
  .payout-row {
    margin: 12px 0 0;
    padding-top: 10px;
    border-top: 1px dashed var(--border);
  }
  .payout-row :global(.info-row:last-child) { border-bottom: none; }
  /* let the value cell overflow so the warning ? tooltip popup isn't clipped */
  .payout-row :global(.info-row dd) { overflow: visible; }

  .stratum-grid {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    gap: 18px;
    flex-wrap: wrap;
  }

  .stratum-grid :global(.stat-field) { text-align: left; }
  .stratum-grid :global(.stat-field:last-child) { text-align: right; }

  @media (max-width: 720px) {
    .stratum-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
      gap: 10px 18px;
    }
    .stratum-grid :global(.stat-field),
    .stratum-grid :global(.stat-field:last-child) { text-align: left; }
  }

  /* card h3 typography lives in ui-kit utilities.css; this page's h3s sit
     inside flex headers that handle their own spacing, so margin: 0 from
     the shared rule is correct as-is. */

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

  .who .sub {
    font-size: 11px;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    margin-top: 4px;
  }

  .status-row :global(.rolling) { min-width: 200px; max-width: 260px; }

  .rolling-eff-label {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--muted);
    text-align: right;
    margin-bottom: 2px;
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

  /* Pool History — uses shared .disclosure utility for the ▸ marker. */
  .pool-history-expander {
    margin-top: 14px;
    padding-top: 10px;
    border-top: 1px dashed var(--border);
  }

  .pool-history-expander > summary h2 {
    font-size: 13px;
    font-weight: 600;
  }

  .pool-history-expander > .stats-list {
    margin-top: 10px;
  }

  .stats-list {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

</style>
