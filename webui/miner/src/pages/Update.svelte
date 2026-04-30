<script lang="ts">
  import { info, otaCheck, otaInstall, otaUpload, rebooting, startRebootRecovery } from '../lib/stores'
  import { fetchOtaCheck, triggerOtaUpdate, fetchOtaStatus, uploadOta } from '../lib/api'
  import { fmtBuildTime } from '../lib/fmt'
  import ConfirmDialog from '../components/ConfirmDialog.svelte'

  let installConfirmOpen = false
  let uploadConfirmOpen = false

  async function handleCheck() {
    otaCheck.set({ checking: true, result: null, msg: 'Checking for updates…', kind: '' })
    const deadline = Date.now() + 15000
    try {
      while (Date.now() < deadline) {
        const res = await fetchOtaCheck()
        if (res !== 'pending') {
          if (res.update_available) {
            otaCheck.set({
              checking: false, result: res,
              msg: `Update available: ${res.latest_version} (current ${res.current_version})`,
              kind: 'avail'
            })
          } else {
            otaCheck.set({
              checking: false, result: res,
              msg: `Firmware is up to date (${res.current_version})`,
              kind: 'ok'
            })
          }
          return
        }
        await new Promise((r) => setTimeout(r, 2000))
      }
      otaCheck.set({ checking: false, result: null, msg: 'Failed to check for updates (timeout).', kind: 'err' })
    } catch (e) {
      otaCheck.set({ checking: false, result: null, msg: `Failed to check: ${(e as Error).message}.`, kind: 'err' })
    }
  }

  function requestInstall() {
    if (!$otaCheck.result?.update_available) return
    installConfirmOpen = true
  }

  async function handleInstall() {
    const current = $otaCheck.result
    if (!current?.update_available) return
    otaInstall.set({ installing: true, pct: 0, state: '', msg: 'Starting OTA install…', kind: '' })
    try {
      await triggerOtaUpdate()
      const deadline = Date.now() + 600000
      while (Date.now() < deadline) {
        const s = await fetchOtaStatus().catch(() => null)
        if (s) {
          /* Skip the brief 'idle' window between triggerOtaUpdate() returning
           * and the OTA task flipping to in_progress — otherwise the bar shows
           * "Idle… 0%" before the first real progress tick. */
          if (s.in_progress || s.progress_pct > 0) {
            otaInstall.set({
              installing: true,
              pct: s.progress_pct,
              state: s.state,
              msg: `${s.state.charAt(0).toUpperCase()}${s.state.slice(1)}… ${s.progress_pct.toFixed(0)}%`,
              kind: ''
            })
          }
          /* Firmware sets state='complete' and progress_pct=100 before the
           * 500ms reboot delay; in_progress stays true until esp_restart().
           * After reboot, status flips back to idle/0/false. So 'complete'
           * is the only reliable success signal — don't gate on !in_progress. */
          if (s.state === 'complete') {
            otaInstall.set({ installing: false, pct: 100, state: s.state, msg: 'Install complete. Miner is rebooting.', kind: 'ok' })
            otaCheck.set({ checking: false, result: null, msg: '', kind: '' })
            startRebootRecovery('Applying firmware update')
            return
          }
          if (s.state === 'error' || (!s.in_progress && s.state !== 'idle' && s.progress_pct < 100)) {
            otaInstall.set({ installing: false, pct: s.progress_pct, state: s.state, msg: `Install ended: ${s.state}.`, kind: 'err' })
            return
          }
        }
        await new Promise((r) => setTimeout(r, 2000))
      }
      otaInstall.update((v) => ({ ...v, installing: false, msg: 'Install timed out.', kind: 'err' }))
    } catch (e) {
      otaInstall.update((v) => ({ ...v, installing: false, msg: `Install failed: ${(e as Error).message}.`, kind: 'err' }))
    }
  }

  // --- Manual upload ---
  let fileInput: HTMLInputElement
  let selectedFile: File | null = null
  let dragOver = false

  function onFileSelect(e: Event) {
    const target = e.target as HTMLInputElement
    selectedFile = target.files?.[0] ?? null
    otaUpload.set({ uploading: false, pct: 0, msg: '', kind: '' })
  }

  function onDrop(e: DragEvent) {
    e.preventDefault()
    dragOver = false
    const f = e.dataTransfer?.files?.[0]
    if (f) {
      selectedFile = f
      otaUpload.set({ uploading: false, pct: 0, msg: '', kind: '' })
    }
  }

  function requestUpload() {
    if (!selectedFile) return
    uploadConfirmOpen = true
  }

  async function handleUpload() {
    if (!selectedFile) return
    otaUpload.set({ uploading: true, pct: 0, msg: 'Uploading… 0%', kind: '' })
    try {
      await uploadOta(selectedFile, (pct) => {
        otaUpload.set({ uploading: true, pct, msg: `Uploading… ${pct.toFixed(0)}%`, kind: '' })
      })
      otaUpload.set({ uploading: false, pct: 100, msg: 'Upload complete. Miner is rebooting to apply the firmware.', kind: 'ok' })
      selectedFile = null
      if (fileInput) fileInput.value = ''
      startRebootRecovery('Applying uploaded firmware')
    } catch (e) {
      otaUpload.update((v) => ({ ...v, uploading: false, msg: `Upload failed: ${(e as Error).message}.`, kind: 'err' }))
    }
  }

  $: firmwareName = $info?.board ? `taipanminer-${$info.board}.bin` : 'firmware.bin'

  /* The miner is unreachable while it reboots — either because the OTA flow
   * just completed (kind==='ok') or because the reboot overlay is up. Gate all
   * action buttons on this so the UI matches what the device can actually do. */
  $: minerBusy = $rebooting.active || $otaInstall.kind === 'ok' || $otaUpload.kind === 'ok'

  /* DEV-only mock panel: pin the UI to specific states so we can iterate on
   * styling and alignment without performing real OTA. Each setter is granular
   * so multiple states can be active at once (e.g. both progress bars). */
  const isDev = !!import.meta.env.DEV

  function mockReset() {
    otaCheck.set({ checking: false, result: null, msg: '', kind: '' })
    otaInstall.set({ installing: false, pct: 0, state: '', msg: '', kind: '' })
    otaUpload.set({ uploading: false, pct: 0, msg: '', kind: '' })
    selectedFile = null
    if (fileInput) fileInput.value = ''
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
    installConfirmOpen = true
  }
  function mockOpenUploadConfirm() {
    if (!selectedFile) {
      const blob = new Blob(['mock firmware'], { type: 'application/octet-stream' })
      selectedFile = new File([blob], firmwareName, { type: 'application/octet-stream' })
    }
    uploadConfirmOpen = true
  }

  function fmtSize(b: number): string {
    if (b < 1024) return `${b} B`
    if (b < 1024 * 1024) return `${(b / 1024).toFixed(0)} KB`
    return `${(b / 1024 / 1024).toFixed(1)} MB`
  }
</script>

<div class="page">
  <!-- Firmware + check -->
  <div class="card">
    <h2>Firmware</h2>
    <div class="info-row"><span class="k">Version</span><span>{$info?.version ?? '—'}</span></div>
    <div class="info-row"><span class="k">Board</span><span>{$info?.board ?? '—'}</span></div>
    <div class="info-row"><span class="k">Build</span><span>{fmtBuildTime($info?.build_date, $info?.build_time)}</span></div>

    <div class="row-actions">
      <button class="btn primary" on:click={handleCheck} disabled={$otaCheck.checking || $otaInstall.installing || minerBusy}>
        {$otaCheck.checking ? 'Checking…' : 'Check for Updates'}
      </button>
      {#if $otaCheck.result?.update_available}
        <button class="btn primary" on:click={requestInstall} disabled={$otaInstall.installing || minerBusy}>
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
    <p class="hint">Upload <code>{firmwareName}</code> directly. The miner flashes to the inactive OTA slot and reboots.</p>

    <div
      class="dropzone"
      class:drag-over={dragOver}
      on:dragover|preventDefault={() => (dragOver = true)}
      on:dragleave={() => (dragOver = false)}
      on:drop={onDrop}
      role="region"
      aria-label="Firmware drop zone"
    >
      {#if selectedFile}
        <div class="file-info">
          <div class="file-name">{selectedFile.name}</div>
          <div class="file-size">{fmtSize(selectedFile.size)}</div>
        </div>
      {:else}
        <div class="dz-msg">
          Drag <code>{firmwareName}</code> here, or
          <button class="btn outline sm" on:click={() => fileInput.click()} type="button" disabled={minerBusy}>choose file</button>
        </div>
      {/if}
      <input type="file" accept=".bin,application/octet-stream" bind:this={fileInput} on:change={onFileSelect} hidden />
    </div>

    {#if selectedFile}
      <div class="row-actions">
        <button class="btn primary" on:click={requestUpload} disabled={$otaUpload.uploading || minerBusy}>
          {$otaUpload.uploading ? `Uploading ${$otaUpload.pct.toFixed(0)}%` : 'Flash firmware'}
        </button>
        <button class="btn outline" on:click={() => { selectedFile = null; if (fileInput) fileInput.value = '' }} disabled={$otaUpload.uploading || minerBusy}>
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
  open={installConfirmOpen}
  title="Install firmware?"
  message={$otaCheck.result
    ? `Install ${$otaCheck.result.latest_version}? The miner will reboot after flashing.`
    : 'Install firmware? The miner will reboot after flashing.'}
  confirmLabel="Install"
  on:confirm={() => { installConfirmOpen = false; handleInstall() }}
  on:cancel={() => (installConfirmOpen = false)}
/>

<ConfirmDialog
  open={uploadConfirmOpen}
  title="Flash firmware?"
  message={selectedFile
    ? `Flash "${selectedFile.name}" (${fmtSize(selectedFile.size)})? The miner will reboot after upload.`
    : 'Flash firmware? The miner will reboot after upload.'}
  confirmLabel="Flash"
  on:confirm={() => { uploadConfirmOpen = false; handleUpload() }}
  on:cancel={() => (uploadConfirmOpen = false)}
/>

<style>
  .page {
    display: flex;
    flex-direction: column;
    gap: 16px;
  }

  h2 {
    color: var(--accent);
    margin: 0 0 14px 0;
    font-size: 14px;
    text-transform: uppercase;
    letter-spacing: 1px;
    display: flex;
    align-items: baseline;
    gap: 10px;
  }

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
