import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render, fireEvent } from '@testing-library/svelte'
import { info, otaCheck, otaInstall, otaUpload, rebooting } from '../lib/stores'

vi.mock('../lib/api', () => ({
  fetchStats: vi.fn(), fetchInfo: vi.fn(), fetchPower: vi.fn(), fetchFan: vi.fn(),
  fetchSettings: vi.fn(), fetchPool: vi.fn(), fetchHealth: vi.fn(), ping: vi.fn(),
  fetchOtaCheck: vi.fn(), triggerOtaUpdate: vi.fn(), fetchOtaStatus: vi.fn(), uploadOta: vi.fn()
}))

// Stub the dev-mock panel so Update tests don't depend on its internals.
// Must be a Svelte 5 component function (not a plain object) so render() can mount it.
vi.mock('../components/UpdateDevMockPanel.svelte', () => {
  const NoOp = (anchor: any) => {
    const el = document.createElement('span')
    anchor.before(el)
    return { destroy() { el.remove() } }
  }
  NoOp.render = () => ({ html: '', head: '', css: { code: '', map: null } })
  return { default: NoOp }
})

// Use vi.hoisted so mockOs is available inside the vi.mock factory (hoisted to top of file)
const mockOs = vi.hoisted(() => ({
  _installConfirmOpen: false,
  _uploadConfirmOpen: false,
  _selectedFile: null as File | null,
  _dragOver: false,
  _fileInput: null as HTMLInputElement | null,
  get installConfirmOpen() { return this._installConfirmOpen },
  set installConfirmOpen(v: boolean) { this._installConfirmOpen = v },
  get uploadConfirmOpen() { return this._uploadConfirmOpen },
  set uploadConfirmOpen(v: boolean) { this._uploadConfirmOpen = v },
  get selectedFile() { return this._selectedFile },
  set selectedFile(v: File | null) { this._selectedFile = v },
  get dragOver() { return this._dragOver },
  get fileInput() { return this._fileInput },
  set fileInput(v: HTMLInputElement | null) { this._fileInput = v },
  handleCheck: vi.fn(),
  handleInstall: vi.fn(),
  handleUpload: vi.fn(),
  requestInstall: vi.fn(),
  requestUpload: vi.fn(),
  onFileSelect: vi.fn(),
  onDrop: vi.fn(),
  onDragOver: vi.fn(),
  onDragLeave: vi.fn(),
}))

vi.mock('../lib/otaState.svelte', () => ({
  createOtaState: vi.fn().mockReturnValue(mockOs)
}))

import Update from './Update.svelte'

const baseInfo = {
  board: 'bitaxe-601', project_name: 'TaipanMiner', version: 'v1.0.0', idf_version: '5.5.3',
  build_date: '2024-01-15', build_time: '14:30:00', chip_model: 'esp32-s3', cores: 2,
  mac: '00:11:22:33:44:55', ssid: 'TestNetwork', flash_size: 16777216, app_size: 1048576,
  total_heap: 262144, free_heap: 131072, reset_reason: 'Unknown', wdt_resets: 0,
  boot_time: 1705333200, worker_name: 'testworker', hostname: 'taipan.local', validated: true
}

describe('Update', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    vi.useFakeTimers()
    info.set(null)
    otaCheck.set({ checking: false, result: null, msg: '', kind: '' })
    otaInstall.set({ installing: false, pct: 0, state: '', msg: '', kind: '' })
    otaUpload.set({ uploading: false, pct: 0, msg: '', kind: '' })
    rebooting.set({ active: false, reason: '', elapsed: 0, timedOut: false })
    // reset mockOs mutable state
    mockOs._installConfirmOpen = false
    mockOs._uploadConfirmOpen = false
    mockOs._selectedFile = null
    mockOs._dragOver = false
    mockOs._fileInput = null
  })

  afterEach(() => {
    vi.useRealTimers()
  })

  it('renders without crashing', () => {
    const result = render(Update)
    expect(result.component).toBeDefined()
  })

  it('renders Firmware and Manual Upload headings', () => {
    const { getByRole } = render(Update)
    expect(getByRole('heading', { name: 'Firmware' })).toBeTruthy()
    expect(getByRole('heading', { name: 'Manual Upload' })).toBeTruthy()
  })

  it('renders with firmware info', () => {
    info.set({ ...baseInfo, version: 'v1.5.2' } as any)
    const { container } = render(Update)
    expect(container.textContent).toContain('v1.5.2')
  })

  it('renders with build date', () => {
    info.set({ ...baseInfo, build_date: '2024-05-01', build_time: '10:30:45' } as any)
    const result = render(Update)
    expect(result.component).toBeDefined()
  })

  it('renders firmware name from board info', () => {
    info.set(baseInfo as any)
    const { container } = render(Update)
    expect(container.textContent).toContain('bitaxe-601')
  })

  it('renders default firmware name when info is null', () => {
    info.set(null)
    const { container } = render(Update)
    expect(container.textContent).toContain('firmware.bin')
  })

  it('renders checking state: Check button shows Checking…', () => {
    otaCheck.set({ checking: true, result: null, msg: 'Checking for updates…', kind: '' })
    const { container } = render(Update)
    expect(container.textContent).toContain('Checking…')
  })

  it('renders otaCheck message when present', () => {
    otaCheck.set({ checking: false, result: null, msg: 'Failed to check: network error.', kind: 'err' })
    const { container } = render(Update)
    expect(container.textContent).toContain('Failed to check: network error.')
  })

  it('renders Install button when update is available', () => {
    otaCheck.set({
      checking: false,
      result: { update_available: true, latest_version: 'v2.0.0', current_version: 'v1.0.0' },
      msg: 'Update available', kind: 'avail'
    })
    const { container } = render(Update)
    expect(container.textContent).toContain('Install v2.0.0')
  })

  it('does NOT render Install button when no update available', () => {
    otaCheck.set({
      checking: false,
      result: { update_available: false, latest_version: 'v1.0.0', current_version: 'v1.0.0' },
      msg: 'Up to date', kind: 'ok'
    })
    const { container } = render(Update)
    expect(container.textContent).not.toContain('Install v')
  })

  it('renders install progress bar when installing', () => {
    otaInstall.set({ installing: true, pct: 45, state: 'downloading', msg: 'Downloading… 45%', kind: '' })
    const { container } = render(Update)
    expect(container.textContent).toContain('Downloading… 45%')
    // progress bar present
    expect(container.querySelector('.progress')).toBeTruthy()
  })

  it('renders install progress bar for non-error msg', () => {
    otaInstall.set({ installing: false, pct: 100, state: 'complete', msg: 'Install complete. Miner is rebooting.', kind: 'ok' })
    const { container } = render(Update)
    expect(container.textContent).toContain('Install complete. Miner is rebooting.')
  })

  it('renders install error msg without progress bar', () => {
    otaInstall.set({ installing: false, pct: 37, state: 'error', msg: 'Install ended: error.', kind: 'err' })
    const { container } = render(Update)
    expect(container.textContent).toContain('Install ended: error.')
  })

  it('renders upload progress bar when uploading', () => {
    otaUpload.set({ uploading: true, pct: 30, msg: 'Uploading… 30%', kind: '' })
    const { container } = render(Update)
    expect(container.textContent).toContain('Uploading… 30%')
  })

  it('renders upload complete message with progress bar', () => {
    otaUpload.set({ uploading: false, pct: 100, msg: 'Upload complete. Miner is rebooting to apply the firmware.', kind: 'ok' })
    const { container } = render(Update)
    expect(container.textContent).toContain('Upload complete. Miner is rebooting to apply the firmware.')
  })

  it('renders upload error without progress bar', () => {
    otaUpload.set({ uploading: false, pct: 73, msg: 'Upload failed: connection reset.', kind: 'err' })
    const { container } = render(Update)
    expect(container.textContent).toContain('Upload failed: connection reset.')
  })

  it('disables Check button when rebooting (busy=true)', () => {
    rebooting.set({ active: true, reason: 'Applying firmware update', elapsed: 5, timedOut: false })
    const { container } = render(Update)
    const checkBtn = container.querySelector('button.btn.primary') as HTMLButtonElement
    expect(checkBtn?.disabled).toBe(true)
  })

  it('renders with multiple states at once', () => {
    otaCheck.set({ checking: false, result: null, msg: '', kind: 'ok' })
    otaInstall.set({ installing: false, pct: 0, state: '', msg: '', kind: '' })
    const result = render(Update)
    expect(result.component).toBeDefined()
  })

  it('renders minerBusy state when rebooting disables Check button', () => {
    rebooting.set({ active: true, reason: 'Test', elapsed: 0, timedOut: false })
    const { container } = render(Update)
    // Check button should be disabled
    const checkBtn = container.querySelector('button.btn.primary') as HTMLButtonElement
    expect(checkBtn?.disabled).toBe(true)
  })

  it('renders minerBusy when install.kind ok — Check button disabled', () => {
    otaInstall.set({ installing: false, pct: 100, state: 'complete', msg: 'ok', kind: 'ok' })
    const { container } = render(Update)
    const checkBtn = container.querySelector('button.btn.primary') as HTMLButtonElement
    expect(checkBtn?.disabled).toBe(true)
  })

  it('renders minerBusy when upload.kind ok — Check button disabled', () => {
    otaUpload.set({ uploading: false, pct: 100, msg: 'ok', kind: 'ok' })
    const { container } = render(Update)
    const checkBtn = container.querySelector('button.btn.primary') as HTMLButtonElement
    expect(checkBtn?.disabled).toBe(true)
  })

  it('renders device info version and board', () => {
    info.set(baseInfo as any)
    const { container } = render(Update)
    expect(container.textContent).toContain('v1.0.0')
    expect(container.textContent).toContain('bitaxe-601')
  })

  it('renders install downloading state', () => {
    otaInstall.set({ installing: true, pct: 50, state: 'downloading', msg: 'Downloading… 50%', kind: '' })
    const { container } = render(Update)
    expect(container.textContent).toContain('Downloading… 50%')
  })

  it('renders install writing state', () => {
    otaInstall.set({ installing: true, pct: 90, state: 'writing', msg: 'Writing… 90%', kind: '' })
    const { container } = render(Update)
    expect(container.textContent).toContain('Writing… 90%')
  })

  it('renders reboot timeout state', () => {
    rebooting.set({ active: true, reason: 'Flash', elapsed: 90, timedOut: true })
    const result = render(Update)
    expect(result.component).toBeDefined()
  })

  it('renders up-to-date message in status', () => {
    otaCheck.set({
      checking: false,
      result: { update_available: false, latest_version: 'v1.0.0', current_version: 'v1.0.0' },
      msg: 'Firmware is up to date (v1.0.0)',
      kind: 'ok'
    })
    const { container } = render(Update)
    expect(container.textContent).toContain('Firmware is up to date (v1.0.0)')
  })

  // ── Handler delegation ──────────────────────────────────────────────────────

  it('Check for Updates button calls os.handleCheck', async () => {
    const { getByRole } = render(Update)
    const btn = getByRole('button', { name: 'Check for Updates' })
    await fireEvent.click(btn)
    expect(mockOs.handleCheck).toHaveBeenCalled()
  })

  it('Install button calls os.requestInstall', async () => {
    otaCheck.set({
      checking: false,
      result: { update_available: true, latest_version: 'v2.0.0', current_version: 'v1.0.0' },
      msg: 'Update available', kind: 'avail'
    })
    const { container } = render(Update)
    const btns = container.querySelectorAll('button.btn.primary')
    // second primary button is the Install button
    await fireEvent.click(btns[1])
    expect(mockOs.requestInstall).toHaveBeenCalled()
  })

  it('choose file button triggers os.fileInput?.click() on the bound input', async () => {
    const { getByRole, container } = render(Update)
    // After render, bind:this has set mockOs._fileInput to the real hidden input
    const realInput = container.querySelector('input[type="file"]') as HTMLInputElement
    const clickSpy = vi.spyOn(realInput, 'click').mockImplementation(() => {})
    const btn = getByRole('button', { name: 'choose file' })
    await fireEvent.click(btn)
    expect(clickSpy).toHaveBeenCalled()
  })

  it('drag-over class applied when os.dragOver is true', () => {
    mockOs._dragOver = true
    const { container } = render(Update)
    const dz = container.querySelector('.dropzone')
    expect(dz?.classList.contains('drag-over')).toBe(true)
    mockOs._dragOver = false
  })

  it('dropzone calls os.onDragLeave on dragleave event', async () => {
    const { container } = render(Update)
    const dz = container.querySelector('.dropzone')!
    await fireEvent.dragLeave(dz)
    expect(mockOs.onDragLeave).toHaveBeenCalled()
  })

  it('dropzone calls os.onDragOver on dragover event', async () => {
    const { container } = render(Update)
    const dz = container.querySelector('.dropzone')!
    await fireEvent.dragOver(dz)
    expect(mockOs.onDragOver).toHaveBeenCalled()
  })

  it('dropzone calls os.onDrop on drop event', async () => {
    const { container } = render(Update)
    const dz = container.querySelector('.dropzone')!
    await fireEvent.drop(dz)
    expect(mockOs.onDrop).toHaveBeenCalled()
  })

  it('renders file name and size when selectedFile is set', () => {
    const fakeFile = new File(['data'], 'miner-firmware.bin', { type: 'application/octet-stream' })
    mockOs._selectedFile = fakeFile
    const { container } = render(Update)
    expect(container.textContent).toContain('miner-firmware.bin')
    mockOs._selectedFile = null
  })

  it('Flash firmware button calls os.requestUpload when file selected', async () => {
    const fakeFile = new File(['data'], 'fw.bin')
    mockOs._selectedFile = fakeFile
    const { getByRole } = render(Update)
    await fireEvent.click(getByRole('button', { name: 'Flash firmware' }))
    expect(mockOs.requestUpload).toHaveBeenCalled()
    mockOs._selectedFile = null
  })

  it('Clear button resets selectedFile and clears fileInput.value', async () => {
    const fakeFile = new File(['data'], 'fw.bin')
    mockOs._selectedFile = fakeFile

    const { getByRole, container } = render(Update)
    // After render, bind:this has populated _fileInput with the real hidden input
    const realInput = container.querySelector('input[type="file"]') as HTMLInputElement

    await fireEvent.click(getByRole('button', { name: 'Clear' }))

    expect(mockOs._selectedFile).toBeNull()
    expect(realInput.value).toBe('')
  })

  it('install confirm dialog open: installConfirmOpen set to true triggers dialog render', () => {
    mockOs._installConfirmOpen = true
    otaCheck.set({
      checking: false,
      result: { update_available: true, latest_version: 'v2.0.0', current_version: 'v1.0.0' },
      msg: '', kind: 'avail'
    })
    const { container } = render(Update)
    // ConfirmDialog is rendered when open=true
    expect(container.textContent).toContain('Install firmware?')
    mockOs._installConfirmOpen = false
  })

  it('otaCheck.msg with kind avail renders with avail styling', () => {
    otaCheck.set({
      checking: false,
      result: { update_available: true, latest_version: 'v2.0.0', current_version: 'v1.0.0' },
      msg: 'Update available: v2.0.0',
      kind: 'avail'
    })
    const { container } = render(Update)
    const status = container.querySelector('.status[data-kind="avail"]')
    expect(status).toBeTruthy()
    expect(status!.textContent).toContain('Update available')
  })

  it('otaInstall.msg with kind err renders without progress bar', () => {
    otaInstall.set({ installing: false, pct: 37, state: 'error', msg: 'Install ended: error.', kind: 'err' })
    const { container } = render(Update)
    const errStatus = container.querySelector('.status[data-kind="err"]')
    expect(errStatus).toBeTruthy()
    // progress-block should NOT be present for err-only state
    expect(container.querySelector('.progress-block')).toBeFalsy()
  })

  it('otaUpload.msg with kind err renders without progress bar', () => {
    otaUpload.set({ uploading: false, pct: 73, msg: 'Upload failed: connection reset.', kind: 'err' })
    const { container } = render(Update)
    expect(container.querySelector('.progress-block')).toBeFalsy()
    const errStatus = container.querySelector('.status[data-kind="err"]')
    expect(errStatus?.textContent).toContain('Upload failed')
  })

  it('installing state shows Uploading pct% text in Flash button', () => {
    const fakeFile = new File(['fw'], 'fw.bin')
    mockOs._selectedFile = fakeFile
    otaUpload.set({ uploading: true, pct: 42, msg: 'Uploading… 42%', kind: '' })
    const { container } = render(Update)
    expect(container.textContent).toContain('Uploading 42%')
    mockOs._selectedFile = null
  })

  it('upload confirm dialog open: uploadConfirmOpen set renders Flash firmware? dialog', () => {
    const fakeFile = new File(['fw'], 'fw.bin')
    mockOs._selectedFile = fakeFile
    mockOs._uploadConfirmOpen = true
    const { container } = render(Update)
    expect(container.textContent).toContain('Flash firmware?')
    mockOs._selectedFile = null
    mockOs._uploadConfirmOpen = false
  })
})
