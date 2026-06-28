<script lang="ts">
  import { stats, info, health, settings } from '../lib/stores'
  import Donut from '../components/Donut.svelte'
  import InfoCard from 'ui-kit/InfoCard.svelte'
  import InfoRow from 'ui-kit/InfoRow.svelte'
  import Tooltip from '../components/Tooltip.svelte'
  import { fmtUnixTs, fmtBuildTime, fmtDuration, rssiBars } from '../lib/fmt'

  /* Layered data sources:
   *   $health — polled every 5s, drives the visual row (live liveness signals)
   *   $info   — fetched once at startup + after a reboot is detected (stores.ts).
   *             Owns identity fields that don't change during a session
   *             (board, MAC, IP, version, reset reason, etc.). */

  type Dot = 'ok' | 'warn' | 'err' | 'idle'
  const healthRows = $derived([
    { label: 'WiFi',     dot: ($health?.network?.connected ? 'ok' : 'err') as Dot },
    { label: 'mDNS',     dot: ($health?.network?.mdns ? 'ok' : 'idle') as Dot },
    { label: 'Knot',     dot: ($health?.knot?.running ? 'ok' : 'idle') as Dot },
    { label: 'Stratum',  dot: ($health?.pool?.stratum ? 'ok' : 'err') as Dot },
    { label: 'Firmware', dot: ($health?.validated === true ? 'ok'
                           : $health?.validated === false ? 'warn'
                           : 'idle') as Dot }
  ])

  const freeHeap = $derived($health?.free_heap ?? null)
  const heapUsed = $derived(
    $info?.heap_internal != null && freeHeap != null
      ? $info.heap_internal.total - freeHeap
      : null
  )
  const rssi = $derived($health?.network?.rssi ?? null)
  const stratumFails = $derived($health?.pool?.fail_count ?? 0)

  // Per-region memory (breadboard B1-310). PSRAM hidden when absent.
  const memInternal = $derived($info?.heap_internal ?? null)
  const memPsram = $derived($info?.heap_psram ?? null)
  const memRtc = $derived($info?.rtc ?? null)

  // ASIC topology: sourced from /api/info fields.
  const hasAsic = $derived(
    $info?.capabilities?.includes('asic') ?? ($info?.mining?.asic != null)
  )
  const asicModel = $derived(
    $info?.mining?.asic
      ? ($info.mining.asic.chips > 1 ? `${$info.mining.asic.model} ×${$info.mining.asic.chips}` : $info.mining.asic.model)
      : undefined
  )
  const detectedChips = $derived($stats?.asic_chips?.length ?? null)
  const expectedChips = $derived($info?.mining?.asic?.chips ?? null)
  const smallCoresPerChip = $derived($stats?.asic_small_cores ?? null)  // per-chip from stats
  const detectedCores = $derived((detectedChips != null && smallCoresPerChip != null) ? detectedChips * smallCoresPerChip : null)
  const expectedCores = $derived(
    ($info?.mining?.asic != null)
      ? $info.mining.asic.chips * $info.mining.asic.small_cores_per_chip
      : (expectedChips != null && smallCoresPerChip != null) ? expectedChips * smallCoresPerChip : null
  )
  const chipsBad = $derived(expectedChips != null && detectedChips != null && detectedChips < expectedChips)

  // Display/LED: sourced from /api/info fields.
  const PANEL_LABEL: Record<string, string> = { st77xx: 'ST77xx', ssd1306: 'SSD1306' }
  const LED_LABEL: Record<string, string> = { apa102: 'APA102', pwm: 'PWM' }
  const displayLabel = $derived(
    $info?.display?.present
      ? (() => {
          const panel = $info!.display!.panel
          const pName = panel ? (PANEL_LABEL[panel] ?? panel.toUpperCase()) : ''
          const res = ($info!.display!.width != null && $info!.display!.height != null)
            ? ` ${$info!.display!.width}×${$info!.display!.height}`
            : ''
          // on/off state now lives in the status-bar Display chip
          return `${pName}${res}`
        })()
      : 'None'
  )
  const ledLabel = $derived(
    $info?.led?.present
      ? (() => {
          const t = $info!.led!.type
          const tName = t ? (LED_LABEL[t] ?? t.toUpperCase()) : ''
          const cnt = $info!.led!.count != null ? ` ×${$info!.led!.count}` : ''
          const rgb = $info!.led!.rgb ? ' · RGB' : ''
          return `${tName}${cnt}${rgb}`
        })()
      : 'None'
  )

</script>

<div class="status-bar">
  <div class="sb-checks">
    {#each healthRows as r (r.label)}
      <span class="h-row" data-state={r.dot}>
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
      </span>
    {/each}
    {#if stratumFails}
      <span class="h-row" data-state="warn">
        <svg viewBox="0 0 20 20" class="h-icon" aria-hidden="true">
          <path d="M10 3 L18 17 L2 17 Z" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round" />
          <path d="M10 8 L10 12 M10 14.5 L10 15" stroke="currentColor" stroke-width="2" stroke-linecap="round" />
        </svg>
        <span class="h-label">{stratumFails} fails</span>
      </span>
    {/if}
  </div>
  {#if $info?.display?.present}
    <Tooltip text="Display {$info.display.enabled ? 'on' : 'off'}">
      <span class="sb-ico" class:sb-off={!$info.display.enabled}>
        <svg viewBox="0 0 20 20" class="h-icon" aria-hidden="true">
          <rect x="3" y="4" width="14" height="10" rx="1.5" fill="none" stroke="currentColor" stroke-width="1.8" />
          <path d="M8 17 L12 17" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" />
        </svg>
      </span>
    </Tooltip>
  {/if}
  {#if $info?.led?.present}
    <Tooltip text="LED {$settings?.led_heartbeat_en ? 'on' : 'off'}">
      <span class="sb-ico" class:sb-off={!$settings?.led_heartbeat_en}>
        <svg viewBox="0 0 20 20" class="h-icon" aria-hidden="true">
          <circle cx="10" cy="10" r="3.5" fill="currentColor" />
          <circle cx="10" cy="10" r="7.5" fill="none" stroke="currentColor" stroke-width="1.4" opacity="0.55" />
        </svg>
      </span>
    </Tooltip>
  {/if}
  <span class="sb-uptime" title="Uptime">
    <svg viewBox="0 0 20 20" class="h-icon" aria-hidden="true">
      <circle cx="10" cy="10" r="7" fill="none" stroke="currentColor" stroke-width="1.6" />
      <path d="M10 6 L10 10 L13 12" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" />
    </svg>
    {fmtDuration($stats?.uptime_s)}
  </span>
</div>

<div class="net-bar">
  <span class="net-item">
    <span class="net-label">Host</span>
    <span class="net-val">{$info?.hostname ?? '—'}</span>
  </span>
  <span class="net-item">
    <span class="net-label">IP</span>
    <span class="net-val mono">{$info?.network?.ip ?? '—'}</span>
  </span>
  <span class="net-item">
    <span class="net-label">SSID</span>
    <span class="net-val">{$info?.network?.ssid ?? '—'}</span>
  </span>
  <span class="net-item">
    <span class="net-label">Signal</span>
    <span class="net-val">{rssi ?? '—'}{#if rssi != null}<span class="dim"> dBm</span> <span class="bars">{rssiBars(rssi)}</span>{/if}</span>
  </span>
</div>

<section class="card resource-card">
  <h3>Resources</h3>
  <div class="visual-row">
    <div class="viz">
      <Donut used={memInternal ? memInternal.total - memInternal.free : heapUsed}
             total={memInternal?.total ?? null} label="SRAM" size={108}
             hint="On-chip static RAM — the chip's internal RAM heap used for stacks, buffers, and allocations. The primary memory budget for firmware (distinct from external PSRAM)." />
    </div>
    {#if memPsram && memPsram.total > 0}
      <div class="viz">
        <Donut used={memPsram.total - memPsram.free} total={memPsram.total} label="PSRAM" size={108}
               hint="External SPI PSRAM — extra RAM on PSRAM-equipped modules. Absent on most boards." />
      </div>
    {/if}
    {#if memRtc}
      <div class="viz">
        <Donut used={memRtc.used} total={memRtc.total} label="RTC" size={108}
               hint="RTC slow memory — a small region that survives deep sleep and soft resets. Used for state that must persist across reboots." />
      </div>
    {/if}
    <div class="viz">
      <Donut used={$info?.build.app_size} total={$info?.build.flash_size} label="Flash" size={108}
             hint="Flash storage — the running app partition's size vs total flash. The remainder holds the second OTA slot, filesystem, and NVS." />
    </div>
  </div>
</section>

<div class="detail-grid">
  {#if hasAsic}
    <InfoCard title="ASIC">
      <InfoRow label="Model">{asicModel ?? '—'}</InfoRow>
      <InfoRow label="Chips" bad={chipsBad}>{detectedChips ?? '—'} <span class="dim">/ {expectedChips ?? '—'}</span></InfoRow>
      <InfoRow label="Small cores" bad={chipsBad}>{detectedCores ?? '—'} <span class="dim">/ {expectedCores ?? '—'}</span></InfoRow>
    </InfoCard>
  {/if}

  <InfoCard title="Device">
    <InfoRow label="Board">{$info?.build.board ?? '—'}</InfoRow>
    <InfoRow label="Chip">{$info?.build.chip_model ?? '—'}</InfoRow>
    <InfoRow label="Cores">{$info?.build.cores ?? '—'}</InfoRow>
    <InfoRow label="MAC" mono>{$info?.mac ?? '—'}</InfoRow>
    <InfoRow label="BSSID" mono>{$info?.network?.bssid ?? '—'}</InfoRow>
    <InfoRow label="Display">{displayLabel}</InfoRow>
    <InfoRow label="LED">{ledLabel}</InfoRow>
  </InfoCard>

  <InfoCard title="Firmware">
    <InfoRow label="Project" mono>{$info?.build.project_name ?? '—'}</InfoRow>
    <InfoRow label="Version">{$info?.build.version ?? '—'}</InfoRow>
    <InfoRow label="Built">{fmtBuildTime($info?.build.build_date, $info?.build.build_time)}</InfoRow>
    <InfoRow label="IDF">{$info?.build.idf_version ?? '—'}</InfoRow>
  </InfoCard>

  <InfoCard title="Runtime">
    <InfoRow label="Reset reason">{$info?.reset_reason ?? '—'}</InfoRow>
    <InfoRow label="WDT resets">{$info?.diag?.wdt_resets ?? '—'}</InfoRow>
    <InfoRow label="Last boot">{fmtUnixTs($info?.boot_epoch_s)}</InfoRow>
  </InfoCard>


</div>

<style>
  .resource-card {
    margin-bottom: 14px;
  }

  .visual-row {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
    gap: 14px;
    align-items: center;
    justify-items: center;
  }

  .viz {
    display: flex;
    justify-content: center;
    align-items: center;
    width: 100%;
  }

  .status-bar {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    gap: 16px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 10px 16px;
    margin-bottom: 14px;
  }

  .sb-checks {
    display: flex;
    flex-wrap: wrap;
    gap: 16px;
    flex: 1;
  }

  /* device-subsystem icons (display / LED) — neutral on/off, never error-red.
     State is conveyed by colour + the hover tooltip. */
  .sb-ico {
    display: inline-flex;
    align-items: center;
    color: var(--success);
  }
  .sb-ico.sb-off { color: var(--muted); }

  .sb-uptime {
    margin-left: auto;
    display: inline-flex;
    align-items: center;
    gap: 6px;
    font-weight: 600;
    font-size: 14px;
    color: var(--text);
    font-variant-numeric: tabular-nums;
  }

  .sb-uptime svg {
    color: var(--accent);
  }

  .net-bar {
    display: flex;
    align-items: center;
    flex-wrap: wrap;
    justify-content: space-between;
    gap: 12px 28px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 10px 16px;
    margin-bottom: 14px;
  }

  .net-item {
    display: inline-flex;
    align-items: baseline;
    gap: 8px;
    min-width: 0;
  }

  .net-label {
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-size: 10px;
    font-weight: 600;
  }

  .net-val {
    color: var(--text);
    font-size: 13px;
    font-variant-numeric: tabular-nums;
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

  .dim { color: var(--muted); }
  .bars { color: var(--accent); margin-left: 3px; }
</style>
