<script lang="ts">
  import { onMount } from 'svelte'
  import { stats, info, fan, hasAsic, fanEditOpen } from '../lib/stores'
  import Toggle from '../components/Toggle.svelte'
  import { createSettingsState } from '../lib/settingsState.svelte'

  const ss = createSettingsState()

  const chipCount = $derived($stats?.asic_count ?? 1)
  const defaultFreq = $derived($stats?.asic_freq_configured_mhz ?? 400)
  const chipIndices = $derived(Array.from({ length: chipCount }, (_, i) => i))

  onMount(() => ss.loadSettings())
</script>

<div class="page">
  {#if ss.loading}
    <div class="loading">Loading settings…</div>
  {:else if ss.loadErr}
    <div class="error">Failed to load settings: {ss.loadErr}</div>
  {:else}
    {#if $hasAsic}
      <!-- ASIC (per-chip) -->
      <h2 class="section-head">
        ASIC <span class="tag pending">TA-97{#if chipCount > 1} · TA-194{/if}</span>
      </h2>
      <div class="settings pending">
        <div class="asic-head">
          <span></span>
          <span class="col-label">Frequency</span>
          <span class="col-label">Core voltage</span>
        </div>
        {#each chipIndices as idx}
          <div class="asic-row">
            <span class="k">Chip {idx}</span>
            <div class="unit-input">
              <input type="number" value={defaultFreq} disabled />
              <span class="unit">MHz</span>
            </div>
            <div class="unit-input">
              <input type="number" value="1150" disabled />
              <span class="unit">mV</span>
            </div>
          </div>
        {/each}
        <p class="hint">
          {#if chipCount > 1}Per-chip tuning (TA-194) lets you bin-tune each die.{:else}Live tuning of ASIC clock and core voltage.{/if}
          Out-of-range settings can brown-out the chip or trip the VR fault.
        </p>
      </div>

      <!-- Fan (TA-315) -->
      <h2 class="section-head">
        Fan
        <button class="btn outline sm" disabled={!$fan} onclick={() => fanEditOpen.set(true)}>Edit</button>
      </h2>
      <div class="settings">
        <div class="row">
          <span class="k">Mode</span>
          <span>{$fan?.autofan ? 'Auto (target temp)' : 'Manual duty'}</span>
        </div>
        {#if $fan?.autofan}
          <div class="row">
            <span class="k">Die target</span>
            <span>{$fan.die_target_c}°C</span>
          </div>
          <div class="row">
            <span class="k">VR target</span>
            <span>{$fan.vr_target_c}°C</span>
          </div>
          <div class="row">
            <span class="k">Minimum fan speed</span>
            <span>{$fan.min_pct}%</span>
          </div>
        {:else if $fan}
          <div class="row">
            <span class="k">Fan speed</span>
            <span>{$fan.manual_pct}%</span>
          </div>
        {/if}
        {#if $fan}
          <p class="hint">
            Live: {$fan.duty_pct ?? '—'}% · {$fan.rpm ?? '—'} rpm
            {#if $fan.autofan && $fan.pid_input_src}
              · PID following {$fan.pid_input_src === 'die' ? 'ASIC' : $fan.pid_input_src.toUpperCase()}
              {#if $fan.pid_input_c != null} ({$fan.pid_input_c.toFixed(1)}°C){/if}
            {/if}
          </p>
        {/if}
      </div>
    {/if}

    <!-- General -->
    <h2 class="section-head">General</h2>
    <div class="settings">
      <div class="row">
        <span class="k">OLED display</span>
        <div class="v">
          <Toggle checked={ss.displayOn} disabled={ss.savingDisplay} onchange={ss.onDisplayChange} />
          {#if ss.displayMsg}<span class="status" data-kind={ss.displayKind}>{ss.displayMsg}</span>{/if}
        </div>
      </div>
      <div class="row">
        <span class="k">OTA check on boot</span>
        <div class="v">
          <Toggle checked={!ss.otaSkip} disabled={ss.savingOtaSkip} onchange={ss.onOtaChange} />
          {#if ss.otaMsg}<span class="status" data-kind={ss.otaKind}>{ss.otaMsg}</span>{/if}
        </div>
      </div>
      <div class="row">
        <span class="k">mDNS</span>
        <div class="v">
          <Toggle checked={ss.mdnsOn} disabled={ss.savingMdns} onchange={ss.onMdnsChange} />
          {#if ss.mdnsMsg}<span class="status" data-kind={ss.mdnsKind}>{ss.mdnsMsg}</span>{/if}
        </div>
      </div>
      <div class="row">
        <span class="k">Knot</span>
        <div class="v">
          <Toggle checked={ss.knotOn} disabled={ss.savingKnot || !ss.mdnsOn} onchange={ss.onKnotChange} />
          {#if ss.knotMsg}<span class="status" data-kind={ss.knotKind}>{ss.knotMsg}</span>{/if}
        </div>
      </div>
    </div>

    <!-- Network / WiFi (hostname lives here) -->
    <h2 class="section-head">Network <span class="tag pending">TA-204 · TA-206</span></h2>
    <div class="settings pending">
      <div class="row">
        <span class="k">Hostname</span>
        <input type="text" placeholder="taipanminer-…" disabled />
      </div>
      <div class="row">
        <span class="k">SSID</span>
        <input type="text" disabled placeholder={$info?.network?.rssi != null ? 'connected network' : ''} />
      </div>
      <div class="row">
        <span class="k">Password</span>
        <input type="password" disabled placeholder="••••••••" />
      </div>
      <p class="hint">
        Hostname is advertised over mDNS and used as the default worker name.
        WiFi changes include automatic rollback if the new network can't be reached.
      </p>
    </div>
  {/if}
</div>

<style>
  .page {
    display: flex;
    flex-direction: column;
    padding-top: 12px;
    max-width: 640px;
  }

  h2 { margin: 24px 0 10px 0; }
  h2:first-of-type { margin-top: 0; }

  .settings {
    display: flex;
    flex-direction: column;
  }

  .settings.pending { opacity: 0.55; }

  .row {
    display: grid;
    grid-template-columns: 180px 1fr;
    align-items: center;
    gap: 12px;
    padding: 8px 0;
    border-bottom: 1px dotted var(--border);
    font-size: 13px;
  }

  .row:last-child { border-bottom: none; }

  .k {
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-size: 11px;
    display: inline-flex;
    align-items: center;
    gap: 6px;
  }

  .v {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  input[type="text"], input[type="password"], input[type="number"] {
    padding: 7px 10px;
    background: var(--input);
    color: var(--text);
    border: 1px solid var(--border);
    border-radius: 4px;
    font-size: 12px;
    font-family: inherit;
    font-variant-numeric: tabular-nums;
  }

  input:focus { outline: none; border-color: var(--accent); }
  input:disabled { opacity: 0.6; cursor: not-allowed; }

  input[type="number"] { width: 90px; }

  .unit-input {
    display: inline-flex;
    align-items: center;
    gap: 6px;
  }

  .unit {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--muted);
  }

  /* ASIC per-chip table */
  .asic-head, .asic-row {
    display: grid;
    grid-template-columns: 100px 1fr 1fr;
    gap: 12px;
    align-items: center;
  }

  .asic-head {
    padding: 4px 0 8px;
    border-bottom: 1px solid var(--border);
  }

  .col-label {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--muted);
  }

  .asic-row {
    padding: 8px 0;
    border-bottom: 1px dotted var(--border);
    font-size: 13px;
  }

  .asic-row:last-of-type { border-bottom: none; }

  .hint {
    margin: 10px 0 0 0;
    font-size: 11px;
    color: var(--muted);
    line-height: 1.5;
  }

  .status {
    font-size: 10px;
    color: var(--success);
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  .status[data-kind="err"] { color: var(--danger); }

  .loading, .error {
    font-size: 12px;
    color: var(--muted);
  }

  .error { color: var(--danger); }

  .section-head {
    display: flex;
    align-items: center;
    gap: 12px;
  }
</style>
