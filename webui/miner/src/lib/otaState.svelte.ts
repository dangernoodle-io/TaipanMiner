import { get } from 'svelte/store'
import { otaCheck, otaInstall, otaUpload, startRebootRecovery } from './stores'
import { fetchOtaCheck, kickOtaCheck, triggerOtaUpdate, fetchOtaStatus, uploadOta } from './api'

export function createOtaState() {
  let installConfirmOpen = $state(false)
  let uploadConfirmOpen = $state(false)
  let selectedFile = $state.raw<File | null>(null)
  let dragOver = $state(false)
  let fileInput = $state.raw<HTMLInputElement | null>(null)

  async function handleCheck() {
    otaCheck.set({ checking: true, result: null, msg: 'Checking for updates…', kind: '' })
    const deadline = Date.now() + 15000
    try {
      const since = await kickOtaCheck()
      while (Date.now() < deadline) {
        const res = await fetchOtaCheck(since)
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
    installConfirmOpen = true
  }

  async function handleInstall() {
    const current = get(otaCheck).result
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

  function requestUpload() {
    if (!selectedFile) return
    uploadConfirmOpen = true
  }

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

  function onDragOver(e: DragEvent) {
    e.preventDefault()
    dragOver = true
  }

  function onDragLeave() {
    dragOver = false
  }

  return {
    get installConfirmOpen() { return installConfirmOpen },
    set installConfirmOpen(v) { installConfirmOpen = v },
    get uploadConfirmOpen() { return uploadConfirmOpen },
    set uploadConfirmOpen(v) { uploadConfirmOpen = v },
    get selectedFile() { return selectedFile },
    set selectedFile(v) { selectedFile = v },
    get dragOver() { return dragOver },
    get fileInput() { return fileInput },
    set fileInput(v) { fileInput = v },
    handleCheck, handleInstall, handleUpload,
    requestInstall, requestUpload,
    onFileSelect, onDrop, onDragOver, onDragLeave,
  }
}
