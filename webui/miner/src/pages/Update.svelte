<script lang="ts">
  import { info, otaCheck, otaInstall, otaUpload, rebooting } from '../lib/stores'
  import { fmtBuildTime, fmtBytes } from '../lib/fmt'
  import ConfirmDialog from '../components/ConfirmDialog.svelte'
  import { createOtaState } from '../lib/otaState.svelte'
  import { firmwareName, minerBusy } from '../lib/otaHelpers'

  const os = createOtaState()

  const fwName = $derived(firmwareName($info))
  const busy = $derived(minerBusy($rebooting, $otaInstall, $otaUpload))

  /* DEV-only mock panel: pin the UI to specific states so we can iterate on
   * styling and alignment without performing real OTA. Each setter is granular
   * so multiple states can be active at once (e.g. both progress bars). */
  const isDev = !!import.meta.env.DEV

  function mockReset() {
    otaCheck.set({ checking: false, result: null, msg: '', kind: '' })
    otaInstall.set({ installing: false, pct: 0, state: '', msg: '', kind: '' })
    otaUpload.set({ uploading: false, pct: 0, msg: '', kind: '' })
    os.selectedFile = null
    if (os.fileInput) os.fileInput.value = ''
    rebooting.set({ active: false, reason: '', elapsed: 0, timedOut: false })
  }

  function mockRebootingOn() {
    rebooting.set({ active: true, reason: 'Applying firmware update (mock)', elapsed: 0, timedOut: false })
  }
  function mockRebootingOff() {
    rebooting.set({ active: false, reason: '', elapsed: 0, timedOut: false })
  }

  function mockCheckChecking() {
    otaCheck.set({ checking: true, result: null, msg: 'Checking for updates…', kind: '' })
  }
  function mockCheckUpToDate() {
    otaCheck.set({
      checking: false,
      result: { update_available: false, latest_version: 'v0.14.0', current_version: 'v0.14.0' } as any,
      msg: 'Firmware is up to date (v0.14.0)',
      kind: 'ok',
    })
  }
  function mockCheckAvailable() {
    otaCheck.set({
      checking: false,
      result: { update_available: true, latest_version: 'v0.99.0-mock', current_version: $info?.version ?? 'v0.14.0' } as any,
      msg: `Update available: v0.99.0-mock (current ${$info?.version ?? 'v0.14.0'})`,
      kind: 'avail',
    })
  }
  function mockCheckError() {
    otaCheck.set({ checking: false, result: null, msg: 'Failed to check: network unreachable (mock)', kind: 'err' })
  }
  function mockInstallStart() {
    otaInstall.set({ installing: true, pct: 0, state: 'starting', msg: 'Starting OTA install…', kind: '' })
  }
  function mockInstallMid() {
    otaInstall.set({ installing: true, pct: 50, state: 'downloading', msg: 'Downloading… 50%', kind: '' })
  }
  function mockInstallNearDone() {
    otaInstall.set({ installing: true, pct: 92, state: 'writing', msg: 'Writing… 92%', kind: '' })
  }
  function mockInstallDone() {
    otaInstall.set({ installing: false, pct: 100, state: 'rebooting', msg: 'Install complete. Miner is rebooting. (mock)', kind: 'ok' })
  }
  function mockInstallError() {
    otaInstall.set({ installing: false, pct: 37, state: 'error', msg: 'Install ended: error. (mock)', kind: 'err' })
  }
  function mockUploadStart() {
    otaUpload.set({ uploading: true, pct: 0, msg: 'Uploading… 0%', kind: '' })
  }
  function mockUploadMid() {
    otaUpload.set({ uploading: true, pct: 50, msg: 'Uploading… 50%', kind: '' })
  }
  function mockUploadDone() {
    otaUpload.set({ uploading: false, pct: 100, msg: 'Upload complete. Miner is rebooting to apply the firmware. (mock)', kind: 'ok' })
  }
  function mockUploadError() {
    otaUpload.set({ uploading: false, pct: 73, msg: 'Upload failed: connection reset. (mock)', kind: 'err' })
  }
  function mockBothProgress() {
    otaInstall.set({ installing: true, pct: 65, state: 'writing', msg: 'Writing… 65%', kind: '' })
    otaUpload.set({ uploading: true, pct: 35, msg: 'Uploading… 35%', kind: '' })
  }
  function mockOpenInstallConfirm() {
    if (!$otaCheck.result?.update_available) mockCheckAvailable()
    os.installConfirmOpen = true
  }
  function mockOpenUploadConfirm() {
    if (!os.selectedFile) {
      const blob = new Blob(['mock firmware'], { type: 'application/octet-stream' })
      os.selectedFile = new File([blob], fwName, { type: 'application/octet-stream' })
    }
    os.uploadConfirmOpen = true
  }

  const fmtSize = fmtBytes
</script>

<div class="page">
  <!-- Firmware + check -->
  <div class="card">
    <h2>Firmware</h2>
    <div class="info-row"><span class="k">Version</span><span>{$info?.version ?? '—'}</span></div>
    <div class="info-row"><span class="k">Board</span><span>{$info?.board ?? '—'}</span></div>
    <div class="info-row"><span class="k">Build</span><span>{fmtBuildTime($info?.build_date, $info?.build_time)}</span></div>

    <div class="row-actions">
      <button class="btn primary" on:click={os.handleCheck} disabled={$otaCheck.checking || $otaInstall.installing || busy}>
        {$otaCheck.checking ? 'Checking…' : 'Check for Updates'}
      </button>
      {#if $otaCheck.result?.update_available}
        <button class="btn primary" on:click={os.requestInstall} disabled={$otaInstall.installing || busy}>
          {$otaInstall.installing ? 'Installing…' : `Install ${$otaCheck.result.latest_version}`}
        </button>
      {/if}
    </div>

    {#if $otaCheck.msg}<div class="status" data-kind={$otaCheck.kind}>{$otaCheck.msg}</div>{/if}

    {#if $otaInstall.installing || ($otaInstall.msg && $otaInstall.kind !== 'err')}
      <div class="progress-block">
        <div class="progress"><div class="progress-fill" style="width: {$otaInstall.pct}%"></div></div>
        {#if $otaInstall.msg}<div class="status" data-kind={$otaInstall.kind}>{$otaInstall.msg}</div>{/if}
      </div>
    {:else if $otaInstall.msg}
      <div class="status" data-kind={$otaInstall.kind}>{$otaInstall.msg}</div>
    {/if}
  </div>

  <!-- Manual upload -->
  <div class="card">
    <h2>Manual Upload</h2>
    <p class="hint">Upload <code>{fwName}</code> directly. The miner flashes to the inactive OTA slot and reboots.</p>

    <div
      class="dropzone"
      class:drag-over={os.dragOver}
      on:dragover={os.onDragOver}
      on:dragleave={os.onDragLeave}
      on:drop={os.onDrop}
      role="region"
      aria-label="Firmware drop zone"
    >
      {#if os.selectedFile}
        <div class="file-info">
          <div class="file-name">{os.selectedFile.name}</div>
          <div class="file-size">{fmtSize(os.selectedFile.size)}</div>
        </div>
      {:else}
        <div class="dz-msg">
          Drag <code>{fwName}</code> here, or
          <button class="btn outline sm" on:click={() => os.fileInput?.click()} type="button" disabled={busy}>choose file</button>
        </div>
      {/if}
      <input type="file" accept=".bin,application/octet-stream" bind:this={os.fileInput} on:change={os.onFileSelect} hidden />
    </div>

    {#if os.selectedFile}
      <div class="row-actions">
        <button class="btn primary" on:click={os.requestUpload} disabled={$otaUpload.uploading || busy}>
          {$otaUpload.uploading ? `Uploading ${$otaUpload.pct.toFixed(0)}%` : 'Flash firmware'}
        </button>
        <button class="btn outline" on:click={() => { os.selectedFile = null; if (os.fileInput) os.fileInput.value = '' }} disabled={$otaUpload.uploading || busy}>
          Clear
        </button>
      </div>
    {/if}

    {#if $otaUpload.uploading || ($otaUpload.msg && $otaUpload.kind !== 'err')}
      <div class="progress-block">
        <div class="progress"><div class="progress-fill" style="width: {$otaUpload.pct}%"></div></div>
        {#if $otaUpload.msg}<div class="status" data-kind={$otaUpload.kind}>{$otaUpload.msg}</div>{/if}
      </div>
    {:else if $otaUpload.msg}
      <div class="status" data-kind={$otaUpload.kind}>{$otaUpload.msg}</div>
    {/if}
  </div>

  {#if isDev}
    <div class="card mock-panel">
      <h2>Mock controls <span class="dev-tag">DEV</span></h2>
      <p class="hint">Pin the UI to specific states without performing a real OTA. States stack — drive both progress bars to inspect alignment.</p>

      <div class="mock-group">
        <span class="mock-label">Reset</span>
        <button class="btn outline sm" on:click={mockReset}>Clear all</button>
      </div>

      <div class="mock-group">
        <span class="mock-label">Check</span>
        <button class="btn outline sm" on:click={mockCheckChecking}>Checking…</button>
        <button class="btn outline sm" on:click={mockCheckUpToDate}>Up to date</button>
        <button class="btn outline sm" on:click={mockCheckAvailable}>Update available</button>
        <button class="btn outline sm" on:click={mockCheckError}>Check error</button>
      </div>

      <div class="mock-group">
        <span class="mock-label">Install</span>
        <button class="btn outline sm" on:click={mockInstallStart}>Starting</button>
        <button class="btn outline sm" on:click={mockInstallMid}>50%</button>
        <button class="btn outline sm" on:click={mockInstallNearDone}>92%</button>
        <button class="btn outline sm" on:click={mockInstallDone}>Complete</button>
        <button class="btn outline sm" on:click={mockInstallError}>Error</button>
      </div>

      <div class="mock-group">
        <span class="mock-label">Upload</span>
        <button class="btn outline sm" on:click={mockUploadStart}>Starting</button>
        <button class="btn outline sm" on:click={mockUploadMid}>50%</button>
        <button class="btn outline sm" on:click={mockUploadDone}>Complete</button>
        <button class="btn outline sm" on:click={mockUploadError}>Error</button>
      </div>

      <div class="mock-group">
        <span class="mock-label">Reboot</span>
        <button class="btn outline sm" on:click={mockRebootingOn}>Rebooting on</button>
        <button class="btn outline sm" on:click={mockRebootingOff}>Rebooting off</button>
      </div>

      <div class="mock-group">
        <span class="mock-label">Combined</span>
        <button class="btn outline sm" on:click={mockBothProgress}>Both progress bars</button>
        <button class="btn outline sm" on:click={mockOpenInstallConfirm}>Install confirm dialog</button>
        <button class="btn outline sm" on:click={mockOpenUploadConfirm}>Upload confirm dialog</button>
      </div>
    </div>
  {/if}
</div>

<ConfirmDialog
  open={os.installConfirmOpen}
  title="Install firmware?"
  message={$otaCheck.result
    ? `Install ${$otaCheck.result.latest_version}? The miner will reboot after flashing.`
    : 'Install firmware? The miner will reboot after flashing.'}
  confirmLabel="Install"
  on:confirm={() => { os.installConfirmOpen = false; os.handleInstall() }}
  on:cancel={() => (os.installConfirmOpen = false)}
/>

<ConfirmDialog
  open={os.uploadConfirmOpen}
  title="Flash firmware?"
  message={os.selectedFile
    ? `Flash "${os.selectedFile.name}" (${fmtSize(os.selectedFile.size)})? The miner will reboot after upload.`
    : 'Flash firmware? The miner will reboot after upload.'}
  confirmLabel="Flash"
  on:confirm={() => { os.uploadConfirmOpen = false; os.handleUpload() }}
  on:cancel={() => (os.uploadConfirmOpen = false)}
/>

<style>
  .page {
    display: flex;
    flex-direction: column;
    gap: 16px;
  }

  /* card h2 typography base from ui-kit; override size (14px here vs 13px global) */
  h2 { margin: 0 0 14px 0; font-size: 14px; }

  .info-row {
    display: flex;
    justify-content: space-between;
    padding: 8px 0;
    border-bottom: 1px dotted var(--border);
    font-size: 13px;
  }

  .info-row .k {
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-size: 11px;
  }

  .info-row span:not(.k) {
    color: var(--text);
    font-family: ui-monospace, Menlo, monospace;
    font-size: 12px;
  }

  .row-actions {
    display: flex;
    gap: 10px;
    flex-wrap: wrap;
    margin-top: 16px;
    align-items: center;
  }

  .status {
    margin-top: 12px;
    font-size: 12px;
    color: var(--label);
  }

  .status[data-kind="ok"] { color: var(--success); }
  .status[data-kind="avail"] { color: var(--warning); }
  .status[data-kind="err"] { color: var(--danger); }

  .hint {
    margin: 0 0 12px 0;
    font-size: 12px;
    color: var(--muted);
  }

  code {
    font-family: ui-monospace, Menlo, monospace;
    font-size: 11px;
    background: var(--input);
    padding: 1px 5px;
    border-radius: 3px;
    color: var(--text);
  }

  .dropzone {
    border: 2px dashed var(--border);
    border-radius: 6px;
    padding: 28px 16px;
    text-align: center;
    transition: border-color 0.15s, background 0.15s;
  }

  .dropzone.drag-over {
    border-color: var(--accent);
    background: rgba(229, 173, 48, 0.05);
  }

  .dz-msg {
    color: var(--muted);
    font-size: 12px;
  }

  .file-info {
    display: flex;
    flex-direction: column;
    gap: 4px;
    align-items: center;
  }

  .file-name {
    color: var(--text);
    font-weight: 600;
    font-size: 13px;
    font-family: ui-monospace, Menlo, monospace;
  }

  .file-size {
    color: var(--muted);
    font-size: 11px;
    font-variant-numeric: tabular-nums;
  }

  /* DEV-only mock panel — dashed border signals it's not part of the real flow.
   * Lifted above the RebootOverlay (z-index 100) so the dev controls remain
   * usable when "Rebooting on" is toggled to inspect the overlay. */
  .mock-panel {
    border-style: dashed;
    opacity: 0.92;
    position: relative;
    z-index: 200;
  }

  .dev-tag {
    font-size: 9px;
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--warning);
    background: rgba(243, 156, 18, 0.12);
    padding: 1px 6px;
    border-radius: 3px;
  }

  .mock-group {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    align-items: center;
    padding: 8px 0;
    border-bottom: 1px dotted var(--border);
  }

  .mock-group:last-child { border-bottom: none; }

  .mock-label {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--muted);
    min-width: 80px;
  }
</style>
