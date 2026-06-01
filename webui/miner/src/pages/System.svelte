<script lang="ts">
  import { stats, info, health, pool } from '../lib/stores'
  import Donut from '../components/Donut.svelte'
  import ConfirmDialog from '../components/ConfirmDialog.svelte'
  import { fmtBytes, fmtUnixTs, fmtBuildTime, fmtDuration, rssiBars } from '../lib/fmt'
  import { resetStats, fetchStats, fetchPool } from '../lib/api'

  /* Layered data sources:
   *   $health — polled every 5s, drives the visual row (live liveness signals)
   *   $info   — fetched once at startup + after a reboot is detected (stores.ts).
   *             Owns identity fields that don't change during a session
   *             (board, MAC, IP, version, reset reason, etc.). */

  type Dot = 'ok' | 'warn' | 'err' | 'idle'
  $: healthRows = [
    { label: 'WiFi',     dot: ($health?.network?.connected ? 'ok' : 'err') as Dot },
    { label: 'mDNS',     dot: ($health?.network?.mdns ? 'ok' : 'idle') as Dot },
    { label: 'Knot',     dot: ($health?.network?.knot ? 'ok' : 'idle') as Dot },
    { label: 'Stratum',  dot: ($health?.network?.stratum ? 'ok' : 'err') as Dot },
    { label: 'Firmware', dot: ($health?.validated === true ? 'ok'
                           : $health?.validated === false ? 'warn'
                           : 'idle') as Dot }
  ]

  $: freeHeap = $health?.free_heap ?? $info?.free_heap ?? null
  $: heapUsed = $info?.total_heap != null && freeHeap != null
    ? $info.total_heap - freeHeap
    : null
  $: rssi = $health?.network?.rssi ?? null
  $: stratumFails = $health?.network?.stratum_fail_count ?? 0

  // ASIC topology: model + expected chip count derive from the board.
  // (Replace with backend fields once /api/stats exposes asic_chip_model + asic_expected_count.)
  const BOARD_ASIC: Record<string, { model: string; expected_chips: number }> = {
    'bitaxe-601': { model: 'BM1370',   expected_chips: 1 },
    'bitaxe-650': { model: 'BM1370 ×2', expected_chips: 2 },
    'bitaxe-403': { model: 'BM1368',   expected_chips: 1 },
  }
  $: asicSpec = $info?.board ? BOARD_ASIC[$info.board] : undefined
  $: detectedChips = $stats?.asic_chips?.length ?? null
  $: smallCoresPerChip = $stats?.asic_small_cores ?? null  // BOARD_SMALL_CORES (per-chip)
  $: detectedCores = (detectedChips != null && smallCoresPerChip != null) ? detectedChips * smallCoresPerChip : null
  $: expectedCores = (asicSpec && smallCoresPerChip != null) ? asicSpec.expected_chips * smallCoresPerChip : null
  $: chipsBad = (asicSpec && detectedChips != null && detectedChips < asicSpec.expected_chips)
  $: hasAsic = asicSpec != null

  let showResetDialog = false
  let resetting = false
  let resetErr = ''

  async function doResetStats() {
    resetting = true
    resetErr = ''
    try {
      await resetStats()
      const [newStats, newPool] = await Promise.all([
        fetchStats().catch(() => null),
        fetchPool().catch(() => null),
      ])
      if (newStats !== null) stats.set(newStats)
      if (newPool !== null) pool.set(newPool)
    } catch (e) {
      resetErr = e instanceof Error ? e.message : 'reset failed'
    } finally {
      resetting = false
    }
  }
</script>

<div class="visual-row">
  <div class="viz health">
    {#each healthRows as r (r.label)}
      <div class="h-row" data-state={r.dot}>
        <svg viewBox="0 0 20 20" class="h-icon" aria-hidden="true">
          {#if r.dot === 'ok'}
            <path d="M4 10 L8 14 L16 6" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" />
          {:else if r.dot === 'err'}
            <path d="M5 5 L15 15 M15 5 L5 15" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" />
          {:else if r.dot === 'warn'}
            <path d="M10 3 L18 17 L2 17 Z" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round" />
            <path d="M10 8 L10 12 M10 14.5 L10 15" stroke="currentColor" stroke-width="2" stroke-linecap="round" />
          {:else}
            <circle cx="10" cy="10" r="6" fill="none" stroke="currentColor" stroke-width="2" />
          {/if}
        </svg>
        <span class="h-label">{r.label}</span>
      </div>
    {/each}
    {#if stratumFails}
      <div class="h-row" data-state="warn">
        <svg viewBox="0 0 20 20" class="h-icon" aria-hidden="true">
          <path d="M10 3 L18 17 L2 17 Z" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round" />
          <path d="M10 8 L10 12 M10 14.5 L10 15" stroke="currentColor" stroke-width="2" stroke-linecap="round" />
        </svg>
        <span class="h-label">{stratumFails} fails</span>
      </div>
    {/if}
  </div>
  <div class="viz">
    <Donut used={heapUsed} total={$info?.total_heap} label="RAM usage" />
  </div>
  <div class="viz">
    <Donut used={$info?.app_size} total={$info?.flash_size} label="Flash" />
  </div>
  <div class="viz uptime">
    <div class="uptime-val">{fmtDuration($stats?.uptime_s)}</div>
    <div class="uptime-label">Uptime</div>
  </div>
</div>

<div class="detail-grid">
  <section class="card">
    <h3>Device</h3>
    <dl>
      <div><dt>Board</dt><dd>{$info?.board ?? '—'}</dd></div>
      <div><dt>Chip</dt><dd>{$info?.chip_model ?? '—'}</dd></div>
      <div><dt>Cores</dt><dd>{$info?.cores ?? '—'}</dd></div>
      <div><dt>MAC</dt><dd class="mono">{$info?.mac ?? '—'}</dd></div>
      <div><dt>IP</dt><dd class="mono">{$info?.network?.ip ?? '—'}</dd></div>
      <div><dt>SSID</dt><dd>{$info?.network?.ssid ?? $info?.ssid ?? '—'}</dd></div>
      <div><dt>BSSID</dt><dd class="mono">{$info?.network?.bssid ?? '—'}</dd></div>
      <div>
        <dt>Signal</dt>
        <dd>
          {rssi ?? '—'}{#if rssi != null}<span class="dim"> dBm</span> <span class="bars">{rssiBars(rssi)}</span>{/if}
        </dd>
      </div>
    </dl>
  </section>

  <section class="card">
    <h3>Firmware</h3>
    <dl>
      <div><dt>Project</dt><dd class="mono small">{$info?.project_name ?? '—'}</dd></div>
      <div><dt>Version</dt><dd>{$info?.version ?? '—'}</dd></div>
      <div><dt>Built</dt><dd>{fmtBuildTime($info?.build_date, $info?.build_time)}</dd></div>
      <div><dt>IDF</dt><dd>{$info?.idf_version ?? '—'}</dd></div>
    </dl>
  </section>

  {#if hasAsic}
    <section class="card">
      <h3>ASIC</h3>
      <dl>
        <div><dt>Model</dt><dd>{asicSpec?.model}</dd></div>
        <div>
          <dt>Chips</dt>
          <dd class:bad={chipsBad}>
            {detectedChips ?? '—'}<span class="dim"> / {asicSpec?.expected_chips}</span>
          </dd>
        </div>
        <div>
          <dt>Small cores</dt>
          <dd class:bad={chipsBad}>
            {detectedCores ?? '—'}<span class="dim"> / {expectedCores ?? '—'}</span>
          </dd>
        </div>
      </dl>
    </section>
  {/if}

  <section class="card">
    <h3>Runtime</h3>
    <dl>
      <div><dt>Reset reason</dt><dd>{$info?.reset_reason ?? '—'}</dd></div>
      <div><dt>WDT resets</dt><dd>{$info?.wdt_resets ?? '—'}</dd></div>
      <div><dt>Last boot</dt><dd>{fmtUnixTs($info?.boot_time)}</dd></div>
    </dl>
  </section>

  <section class="card">
    <h3>Actions</h3>
    <div class="action-row">
      <button class="btn danger sm" onclick={() => { showResetDialog = true }} disabled={resetting}>
        {resetting ? 'Resetting…' : 'Reset stats'}
      </button>
    </div>
    {#if resetErr}<div class="action-err">{resetErr}</div>{/if}
  </section>
</div>

<ConfirmDialog
  bind:open={showResetDialog}
  title="Reset stats?"
  message="This will clear all session and lifetime mining statistics. This action cannot be undone."
  confirmLabel="Reset"
  danger
  onconfirm={doResetStats}
/>

<style>
  .visual-row {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(160px, 1fr));
    gap: 16px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 20px 16px;
    margin-bottom: 14px;
    align-items: center;
    justify-items: center;
  }

  .viz {
    display: flex;
    justify-content: center;
    align-items: center;
    width: 100%;
  }

  .viz.uptime {
    flex-direction: column;
    gap: 6px;
  }

  .uptime-val {
    font-size: 28px;
    font-weight: 600;
    color: var(--accent);
    font-variant-numeric: tabular-nums;
    line-height: 1;
  }

  .uptime-label {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    font-weight: 600;
  }

  .viz.health {
    flex-direction: column;
    align-items: stretch;
    gap: 6px;
    min-width: 140px;
    max-width: 180px;
  }

  .h-row {
    display: grid;
    grid-template-columns: 16px 1fr;
    align-items: center;
    gap: 10px;
    color: var(--muted);
  }

  .h-icon {
    width: 14px;
    height: 14px;
  }

  .h-row[data-state="ok"]   { color: var(--success); }
  .h-row[data-state="warn"] { color: var(--warning); }
  .h-row[data-state="err"]  { color: var(--danger); }

  .h-label {
    color: var(--text);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-size: 11px;
    font-weight: 600;
  }

  .detail-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(240px, 1fr));
    gap: 14px;
  }

  /* card h3 typography lives in ui-kit utilities.css. */
  h3 { margin-bottom: 12px; }

  dl { margin: 0; }

  dl > div {
    display: grid;
    grid-template-columns: 1fr auto;
    gap: 12px;
    align-items: baseline;
    min-width: 0;
    font-size: 12px;
    border-bottom: 1px dotted var(--border);
    padding: 6px 0;
  }

  dl > div:last-child { border-bottom: none; }

  dt {
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-size: 10px;
  }

  dd {
    margin: 0;
    color: var(--text);
    font-variant-numeric: tabular-nums;
    text-align: right;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .mono {
    font-family: ui-monospace, Menlo, monospace;
    font-size: 11px;
  }

  .small { font-size: 11px; }

  .dim { color: var(--muted); }
  dd.bad { color: var(--warning); }
  .bars { color: var(--accent); margin-left: 3px; }

  .action-row {
    display: flex;
    gap: 10px;
    flex-wrap: wrap;
  }

  .action-err {
    margin-top: 8px;
    font-size: 12px;
    color: var(--danger);
  }
</style>
