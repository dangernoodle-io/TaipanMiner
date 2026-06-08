<script lang="ts">
  import { info, otaCheck, otaInstall, otaUpload, rebooting } from '../lib/stores'
  import { fmtBuildTime, fmtBytes } from '../lib/fmt'
  import ConfirmDialog from '../components/ConfirmDialog.svelte'
  import UpdateDevMockPanel from '../components/UpdateDevMockPanel.svelte'
  import InfoRow from 'ui-kit/InfoRow.svelte'
  import { createOtaState } from '../lib/otaState.svelte'
  import { firmwareName, minerBusy } from '../lib/otaHelpers'

  const os = createOtaState()

  const fwName = $derived(firmwareName($info))
  const busy = $derived(minerBusy($rebooting, $otaInstall, $otaUpload))

  /* DEV-only mock panel: pin the UI to specific states so we can iterate on
   * styling and alignment without performing real OTA. */
  const isDev = !!import.meta.env.DEV

  const fmtSize = fmtBytes
</script>

<div class="page">
  <!-- Firmware + check -->
  <div class="card">
    <h2>Firmware</h2>
    <dl class="fw-rows">
      <InfoRow label="Version">{$info?.version ?? '—'}</InfoRow>
      <InfoRow label="Board">{$info?.board ?? '—'}</InfoRow>
      <InfoRow label="Build">{fmtBuildTime($info?.build_date, $info?.build_time)}</InfoRow>
    </dl>

    <div class="row-actions">
      <button class="btn primary" onclick={os.handleCheck} disabled={$otaCheck.checking || $otaInstall.installing || busy}>
        {$otaCheck.checking ? 'Checking…' : 'Check for Updates'}
      </button>
      {#if $otaCheck.result?.update_available}
        <button class="btn primary" onclick={os.requestInstall} disabled={$otaInstall.installing || busy}>
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
      ondragover={os.onDragOver}
      ondragleave={os.onDragLeave}
      ondrop={os.onDrop}
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
          <button class="btn outline sm" onclick={() => os.fileInput?.click()} type="button" disabled={busy}>choose file</button>
        </div>
      {/if}
      <input type="file" accept=".bin,application/octet-stream" bind:this={os.fileInput} onchange={os.onFileSelect} hidden />
    </div>

    {#if os.selectedFile}
      <div class="row-actions">
        <button class="btn primary" onclick={os.requestUpload} disabled={$otaUpload.uploading || busy}>
          {$otaUpload.uploading ? `Uploading ${$otaUpload.pct.toFixed(0)}%` : 'Flash firmware'}
        </button>
        <button class="btn outline" onclick={() => { os.selectedFile = null; if (os.fileInput) os.fileInput.value = '' }} disabled={$otaUpload.uploading || busy}>
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
    <UpdateDevMockPanel {os} />
  {/if}
</div>

<ConfirmDialog
  open={os.installConfirmOpen}
  title="Install firmware?"
  message={$otaCheck.result
    ? `Install ${$otaCheck.result.latest_version}? The miner will reboot after flashing.`
    : 'Install firmware? The miner will reboot after flashing.'}
  confirmLabel="Install"
  onconfirm={() => { os.installConfirmOpen = false; os.handleInstall() }}
  oncancel={() => (os.installConfirmOpen = false)}
/>

<ConfirmDialog
  open={os.uploadConfirmOpen}
  title="Flash firmware?"
  message={os.selectedFile
    ? `Flash "${os.selectedFile.name}" (${fmtSize(os.selectedFile.size)})? The miner will reboot after upload.`
    : 'Flash firmware? The miner will reboot after upload.'}
  confirmLabel="Flash"
  onconfirm={() => { os.uploadConfirmOpen = false; os.handleUpload() }}
  oncancel={() => (os.uploadConfirmOpen = false)}
/>

<style>
  .page {
    display: flex;
    flex-direction: column;
    gap: 16px;
  }

  /* card h2 typography base from ui-kit; override size (14px here vs 13px global) */
  h2 { margin: 0 0 14px 0; font-size: 14px; }

  .fw-rows {
    margin: 0 0 4px;
  }
  /* drop the dotted separator under the last row before the action buttons */
  .fw-rows :global(.info-row:last-child) { border-bottom: none; }

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

</style>
