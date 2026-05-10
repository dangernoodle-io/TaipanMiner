import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'
import { render } from '@testing-library/svelte'
import { info, otaCheck, otaInstall, otaUpload, rebooting } from '../lib/stores'

vi.mock('../lib/api', () => ({
  fetchStats: vi.fn(), fetchInfo: vi.fn(), fetchPower: vi.fn(), fetchFan: vi.fn(),
  fetchSettings: vi.fn(), fetchPool: vi.fn(), fetchHealth: vi.fn(), ping: vi.fn(),
  fetchOtaCheck: vi.fn(), triggerOtaUpdate: vi.fn(), fetchOtaStatus: vi.fn(), uploadOta: vi.fn()
}))

vi.mock('../lib/otaState.svelte', () => ({
  createOtaState: vi.fn().mockReturnValue({
    get installConfirmOpen() { return false },
    set installConfirmOpen(_v: boolean) {},
    get uploadConfirmOpen() { return false },
    set uploadConfirmOpen(_v: boolean) {},
    get selectedFile() { return null },
    set selectedFile(_v: File | null) {},
    get dragOver() { return false },
    get fileInput() { return null },
    set fileInput(_v: HTMLInputElement | null) {},
    handleCheck: vi.fn(),
    handleInstall: vi.fn(),
    handleUpload: vi.fn(),
    requestInstall: vi.fn(),
    requestUpload: vi.fn(),
    onFileSelect: vi.fn(),
    onDrop: vi.fn(),
    onDragOver: vi.fn(),
    onDragLeave: vi.fn(),
  })
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

  it('renders reboot overlay when rebooting', () => {
    rebooting.set({ active: true, reason: 'Applying firmware update', elapsed: 5, timedOut: false })
    const { container } = render(Update)
    expect(container.textContent).toContain('Rebooting')
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
})
