<script lang="ts">
  import { info, otaCheck, otaInstall, otaUpload, rebooting } from '../lib/stores'
  import { firmwareName } from '../lib/otaHelpers'
  import { createOtaState } from '../lib/otaState.svelte'

  // os is only needed for the confirm-dialog shortcuts that read/write os state
  const { os }: { os: ReturnType<typeof createOtaState> } = $props()

  const fwName = $derived(firmwareName($info))

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
      result: { update_available: true, latest_version: 'v0.99.0-mock', current_version: $info?.build.version ?? 'v0.14.0' } as any,
      msg: `Update available: v0.99.0-mock (current ${$info?.build.version ?? 'v0.14.0'})`,
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
</script>

<div class="card mock-panel">
  <h2>Mock controls <span class="dev-tag">DEV</span></h2>
  <p class="hint">Pin the UI to specific states without performing a real OTA. States stack — drive both progress bars to inspect alignment.</p>

  <div class="mock-group">
    <span class="mock-label">Reset</span>
    <button class="btn outline sm" onclick={mockReset}>Clear all</button>
  </div>

  <div class="mock-group">
    <span class="mock-label">Check</span>
    <button class="btn outline sm" onclick={mockCheckChecking}>Checking…</button>
    <button class="btn outline sm" onclick={mockCheckUpToDate}>Up to date</button>
    <button class="btn outline sm" onclick={mockCheckAvailable}>Update available</button>
    <button class="btn outline sm" onclick={mockCheckError}>Check error</button>
  </div>

  <div class="mock-group">
    <span class="mock-label">Install</span>
    <button class="btn outline sm" onclick={mockInstallStart}>Starting</button>
    <button class="btn outline sm" onclick={mockInstallMid}>50%</button>
    <button class="btn outline sm" onclick={mockInstallNearDone}>92%</button>
    <button class="btn outline sm" onclick={mockInstallDone}>Complete</button>
    <button class="btn outline sm" onclick={mockInstallError}>Error</button>
  </div>

  <div class="mock-group">
    <span class="mock-label">Upload</span>
    <button class="btn outline sm" onclick={mockUploadStart}>Starting</button>
    <button class="btn outline sm" onclick={mockUploadMid}>50%</button>
    <button class="btn outline sm" onclick={mockUploadDone}>Complete</button>
    <button class="btn outline sm" onclick={mockUploadError}>Error</button>
  </div>

  <div class="mock-group">
    <span class="mock-label">Reboot</span>
    <button class="btn outline sm" onclick={mockRebootingOn}>Rebooting on</button>
    <button class="btn outline sm" onclick={mockRebootingOff}>Rebooting off</button>
  </div>

  <div class="mock-group">
    <span class="mock-label">Combined</span>
    <button class="btn outline sm" onclick={mockBothProgress}>Both progress bars</button>
    <button class="btn outline sm" onclick={mockOpenInstallConfirm}>Install confirm dialog</button>
    <button class="btn outline sm" onclick={mockOpenUploadConfirm}>Upload confirm dialog</button>
  </div>
</div>

<style>
  /* DEV-only mock panel — dashed border signals it's not part of the real flow.
   * Lifted above the RebootOverlay (z-index 100) so the dev controls remain
   * usable when "Rebooting on" is toggled to inspect the overlay. */
  .mock-panel {
    border-style: dashed;
    opacity: 0.92;
    position: relative;
    z-index: 200;
  }

  h2 { margin: 0 0 14px 0; font-size: 14px; }

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

  .hint {
    margin: 0 0 12px 0;
    font-size: 12px;
    color: var(--muted);
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
