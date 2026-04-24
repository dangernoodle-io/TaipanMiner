<script lang="ts">
  import { stats, info, otaCheck, otaInstall } from '../lib/stores'
  import { fetchOtaCheck, triggerOtaUpdate, fetchOtaStatus, uploadOta } from '../lib/api'
  import { fmtBuildTime } from '../lib/fmt'

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
      otaCheck.set({ checking: false, result: null, msg: `Failed to check: ${(e as Error).message}`, kind: 'err' })
    }
  }

  async function handleInstall() {
    const current = $otaCheck.result
    if (!current?.update_available) return
    if (!confirm(`Install ${current.latest_version}? The miner will reboot after flashing.`)) return
    otaInstall.set({ installing: true, pct: 0, state: '', msg: 'Starting OTA install…', kind: '' })
    try {
      await triggerOtaUpdate()
      const deadline = Date.now() + 600000
      while (Date.now() < deadline) {
        const s = await fetchOtaStatus().catch(() => null)
        if (s) {
          otaInstall.set({
            installing: true,
            pct: s.progress_pct,
            state: s.state,
            msg: `${s.state} — ${s.progress_pct.toFixed(0)}%`,
            kind: ''
          })
          if (!s.in_progress && s.progress_pct >= 100) {
            otaInstall.set({ installing: false, pct: 100, state: s.state, msg: 'Install complete. Miner is rebooting.', kind: 'ok' })
            otaCheck.set({ checking: false, result: null, msg: '', kind: '' })
            return
          }
          if (!s.in_progress && s.state !== 'idle') {
            otaInstall.set({ installing: false, pct: s.progress_pct, state: s.state, msg: `Install ended: ${s.state}`, kind: 'err' })
            return
          }
        }
        await new Promise((r) => setTimeout(r, 2000))
      }
      otaInstall.update((v) => ({ ...v, installing: false, msg: 'Install timed out.', kind: 'err' }))
    } catch (e) {
      otaInstall.update((v) => ({ ...v, installing: false, msg: `Install failed: ${(e as Error).message}`, kind: 'err' }))
    }
  }

  // --- Manual upload ---
  let fileInput: HTMLInputElement
  let selectedFile: File | null = null
  let uploading = false
  let uploadPct = 0
  let uploadMsg = ''
  let uploadKind: '' | 'ok' | 'err' = ''
  let dragOver = false

  function onFileSelect(e: Event) {
    const target = e.target as HTMLInputElement
    selectedFile = target.files?.[0] ?? null
    uploadMsg = ''
    uploadKind = ''
  }

  function onDrop(e: DragEvent) {
    e.preventDefault()
    dragOver = false
    const f = e.dataTransfer?.files?.[0]
    if (f) {
      selectedFile = f
      uploadMsg = ''
      uploadKind = ''
    }
  }

  async function handleUpload() {
    if (!selectedFile) return
    if (!confirm(`Flash "${selectedFile.name}" (${fmtSize(selectedFile.size)})? The miner will reboot after upload.`)) return
    uploading = true
    uploadPct = 0
    uploadMsg = 'Uploading…'
    uploadKind = ''
    try {
      await uploadOta(selectedFile, (pct) => (uploadPct = pct))
      uploadKind = 'ok'
      uploadMsg = 'Upload complete. Miner is rebooting to apply the firmware.'
      selectedFile = null
      if (fileInput) fileInput.value = ''
    } catch (e) {
      uploadKind = 'err'
      uploadMsg = `Upload failed: ${(e as Error).message}`
    } finally {
      uploading = false
    }
  }

  $: firmwareName = $stats?.board ? `taipanminer-${$stats.board}.bin` : 'firmware.bin'

  function fmtSize(b: number): string {
    if (b < 1024) return `${b} B`
    if (b < 1024 * 1024) return `${(b / 1024).toFixed(0)} KB`
    return `${(b / 1024 / 1024).toFixed(1)} MB`
  }
</script>

<div class="page">
  <!-- Firmware + check -->
  <div class="section">
    <h2>Firmware</h2>
    <div class="info-row"><span class="k">Version</span><span>{$stats?.version ?? '—'}</span></div>
    <div class="info-row"><span class="k">Board</span><span>{$stats?.board ?? '—'}</span></div>
    <div class="info-row"><span class="k">Build</span><span>{fmtBuildTime($info?.build_date, $info?.build_time)}</span></div>

    <div class="row-actions">
      <button class="btn primary" on:click={handleCheck} disabled={$otaCheck.checking || $otaInstall.installing}>
        {$otaCheck.checking ? 'Checking…' : 'Check for Updates'}
      </button>
      {#if $otaCheck.result?.update_available}
        <button class="btn primary" on:click={handleInstall} disabled={$otaInstall.installing}>
          {$otaInstall.installing ? 'Installing…' : `Install ${$otaCheck.result.latest_version}`}
        </button>
      {/if}
    </div>

    {#if $otaCheck.msg}<div class="status" data-kind={$otaCheck.kind}>{$otaCheck.msg}</div>{/if}

    {#if $otaInstall.installing || $otaInstall.msg}
      <div class="install-progress">
        <div class="progress"><div class="progress-fill" style="width: {$otaInstall.pct}%"></div></div>
        <div class="status" data-kind={$otaInstall.kind}>{$otaInstall.msg}</div>
      </div>
    {/if}
  </div>

  <!-- Manual upload -->
  <div class="section">
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
          <button class="btn outline sm" on:click={() => fileInput.click()} type="button">choose file</button>
        </div>
      {/if}
      <input type="file" accept=".bin,application/octet-stream" bind:this={fileInput} on:change={onFileSelect} hidden />
    </div>

    {#if selectedFile}
      <div class="row-actions">
        <button class="btn primary" on:click={handleUpload} disabled={uploading}>
          {uploading ? `Uploading ${uploadPct.toFixed(0)}%` : 'Flash firmware'}
        </button>
        <button class="btn outline" on:click={() => { selectedFile = null; if (fileInput) fileInput.value = '' }} disabled={uploading}>
          Clear
        </button>
      </div>

      {#if uploading}
        <div class="progress"><div class="progress-fill" style="width: {uploadPct}%"></div></div>
      {/if}
    {/if}

    {#if uploadMsg}<div class="status" data-kind={uploadKind}>{uploadMsg}</div>{/if}
  </div>
</div>

<style>
  .page {
    display: flex;
    flex-direction: column;
    gap: 28px;
  }

  h2 {
    color: var(--accent);
    margin: 0 0 14px 0;
    font-size: 14px;
    text-transform: uppercase;
    letter-spacing: 1px;
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

  .info-row strong,
  .info-row span:not(.k) {
    color: var(--text);
    font-family: ui-monospace, Menlo, monospace;
    font-weight: normal;
    font-size: 12px;
  }

  .info-row strong { font-weight: 600; }

  .row-actions {
    display: flex;
    gap: 10px;
    flex-wrap: wrap;
    margin-top: 16px;
  }

  .btn {
    border: 1px solid var(--border);
    background: var(--surface);
    color: var(--text);
    padding: 10px 20px;
    font-size: 12px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-weight: 600;
    border-radius: 4px;
    cursor: pointer;
    transition: background 0.15s, color 0.15s, border-color 0.15s;
  }

  .btn.sm { padding: 5px 12px; font-size: 10px; display: inline; }

  .btn.primary {
    background: var(--accent);
    color: var(--bg);
    border-color: var(--accent);
  }

  .btn.primary:hover:not(:disabled) { background: var(--accent-hover); }

  .btn.outline {
    background: transparent;
    color: var(--label);
  }

  .btn.outline:hover:not(:disabled) {
    color: var(--text);
    border-color: var(--label);
  }

  .btn:disabled { opacity: 0.4; cursor: not-allowed; }

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

  .progress {
    height: 6px;
    background: var(--border);
    border-radius: 3px;
    overflow: hidden;
    margin-top: 12px;
  }

  .progress-fill {
    height: 100%;
    background: var(--accent);
    transition: width 0.2s ease;
  }

  .install-progress {
    margin-top: 14px;
  }
</style>
