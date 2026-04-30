<script lang="ts">
  import { onMount } from 'svelte'
  import { fetchSettings, patchSettings, type Settings } from '../lib/api'
  import { stats, info, hasAsic } from '../lib/stores'
  import Toggle from '../components/Toggle.svelte'

  let loading = true
  let loadErr = ''
  let saved: Settings | null = null

  let displayOn = false
  let otaSkip = false
  let savingDisplay = false
  let savingOtaSkip = false
  let displayMsg = ''
  let otaMsg = ''
  let displayKind: '' | 'ok' | 'err' = ''
  let otaKind: '' | 'ok' | 'err' = ''

  $: chipCount = $stats?.asic_count ?? 1
  $: defaultFreq = $stats?.asic_freq_configured_mhz ?? 400
  $: chipIndices = Array.from({ length: chipCount }, (_, i) => i)

  async function load() {
    loading = true
    loadErr = ''
    try {
      const s = await fetchSettings()
      saved = s
      displayOn = !!saved.display_en
      otaSkip = !!saved.ota_skip_check
    } catch (e) {
      loadErr = (e as Error).message
    } finally {
      loading = false
    }
  }

  onMount(load)

  function onDisplayChange(e: Event) {
    saveDisplay((e.currentTarget as HTMLInputElement).checked)
  }
  function onOtaChange(e: Event) {
    // checkbox is "OTA check on boot" — checked = check enabled = skip=false
    saveOtaSkip(!(e.currentTarget as HTMLInputElement).checked)
  }

  async function saveDisplay(next: boolean) {
    savingDisplay = true
    displayMsg = ''
    displayKind = ''
    try {
      const res = await patchSettings({ display_en: next })
      displayOn = next
      displayKind = 'ok'
      displayMsg = res.reboot_required ? 'Saved — reboot to apply' : 'Saved'
    } catch (e) {
      displayOn = saved?.display_en ?? !next
      displayKind = 'err'
      displayMsg = (e as Error).message
    } finally {
      savingDisplay = false
    }
  }

  async function saveOtaSkip(next: boolean) {
    savingOtaSkip = true
    otaMsg = ''
    otaKind = ''
    try {
      const res = await patchSettings({ ota_skip_check: next })
      otaSkip = next
      otaKind = 'ok'
      otaMsg = res.reboot_required ? 'Saved — reboot to apply' : 'Saved'
    } catch (e) {
      otaSkip = saved?.ota_skip_check ?? !next
      otaKind = 'err'
      otaMsg = (e as Error).message
    } finally {
      savingOtaSkip = false
    }
  }
</script>

<div class="page">
  {#if loading}
    <div class="loading">Loading settings…</div>
  {:else if loadErr}
    <div class="error">Failed to load settings: {loadErr}</div>
  {:else}
    {#if $hasAsic}
      <!-- ASIC (per-chip) -->
      <h2>
        ASIC <span class="pending-tag">TA-97{#if chipCount > 1} · TA-194{/if}</span>
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

      <!-- Fan -->
      <h2>Fan <span class="pending-tag">TA-141</span></h2>
      <div class="settings pending">
        <div class="row">
          <span class="k">Mode</span>
          <select disabled>
            <option>Auto (target temp)</option>
            <option>Manual duty</option>
          </select>
        </div>
        <div class="row">
          <span class="k">Target temperature</span>
          <div class="unit-input">
            <input type="number" value="65" disabled />
            <span class="unit">°C</span>
          </div>
        </div>
        <p class="hint">Closed-loop fan control keeps the ASIC at a target temperature.</p>
      </div>
    {/if}

    <!-- General -->
    <h2>General</h2>
    <div class="settings">
      <div class="row">
        <span class="k">OLED display</span>
        <div class="v">
          <Toggle checked={displayOn} disabled={savingDisplay} on:change={onDisplayChange} />
          {#if displayMsg}<span class="status" data-kind={displayKind}>{displayMsg}</span>{/if}
        </div>
      </div>
      <div class="row">
        <span class="k">OTA check on boot</span>
        <div class="v">
          <Toggle checked={!otaSkip} disabled={savingOtaSkip} on:change={onOtaChange} />
          {#if otaMsg}<span class="status" data-kind={otaKind}>{otaMsg}</span>{/if}
        </div>
      </div>
    </div>

    <!-- Network / WiFi (hostname lives here) -->
    <h2>Network <span class="pending-tag">TA-204 · TA-206</span></h2>
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

  h2 {
    color: var(--accent);
    margin: 24px 0 10px 0;
    font-size: 13px;
    text-transform: uppercase;
    letter-spacing: 1px;
    display: flex;
    align-items: baseline;
    gap: 10px;
  }

  h2:first-of-type { margin-top: 0; }

  .pending-tag {
    font-size: 9px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--warning);
    background: rgba(243, 156, 18, 0.12);
    padding: 1px 6px;
    border-radius: 3px;
  }

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

  input[type="text"], input[type="password"], input[type="number"], select {
    padding: 7px 10px;
    background: var(--input);
    color: var(--text);
    border: 1px solid var(--border);
    border-radius: 4px;
    font-size: 12px;
    font-family: inherit;
    font-variant-numeric: tabular-nums;
  }

  input:focus, select:focus { outline: none; border-color: var(--accent); }
  input:disabled, select:disabled { opacity: 0.6; cursor: not-allowed; }

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

  .toggle {
    display: inline-flex;
    align-items: center;
    gap: 8px;
    cursor: pointer;
    color: var(--text);
    font-size: 12px;
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
</style>
