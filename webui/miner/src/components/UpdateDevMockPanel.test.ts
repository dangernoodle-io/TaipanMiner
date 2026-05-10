import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, fireEvent } from '@testing-library/svelte'
import { get } from 'svelte/store'
import { otaCheck, otaInstall, otaUpload, rebooting, info } from '../lib/stores'
import UpdateDevMockPanel from './UpdateDevMockPanel.svelte'
import { createOtaState } from '../lib/otaState.svelte'

vi.mock('../lib/api', () => ({
  fetchStats: vi.fn(), fetchInfo: vi.fn(), fetchPower: vi.fn(), fetchFan: vi.fn(),
  fetchSettings: vi.fn(), fetchPool: vi.fn(), fetchHealth: vi.fn(), ping: vi.fn(),
  fetchOtaCheck: vi.fn(), triggerOtaUpdate: vi.fn(), fetchOtaStatus: vi.fn(), uploadOta: vi.fn(),
}))

function makeOs() {
  return createOtaState()
}

function resetStores() {
  info.set(null)
  otaCheck.set({ checking: false, result: null, msg: '', kind: '' })
  otaInstall.set({ installing: false, pct: 0, state: '', msg: '', kind: '' })
  otaUpload.set({ uploading: false, pct: 0, msg: '', kind: '' })
  rebooting.set({ active: false, reason: '', elapsed: 0, timedOut: false })
}

describe('UpdateDevMockPanel', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    resetStores()
  })

  it('renders the mock panel heading', () => {
    const os = makeOs()
    const { container } = render(UpdateDevMockPanel, { props: { os } })
    expect(container.textContent).toContain('Mock controls')
    expect(container.textContent).toContain('DEV')
  })

  it('renders all group labels', () => {
    const os = makeOs()
    const { container } = render(UpdateDevMockPanel, { props: { os } })
    const text = container.textContent!
    expect(text).toContain('Reset')
    expect(text).toContain('Check')
    expect(text).toContain('Install')
    expect(text).toContain('Upload')
    expect(text).toContain('Reboot')
    expect(text).toContain('Combined')
  })

  // ── mockReset ──────────────────────────────────────────────────────────────

  it('Clear all: resets all stores to initial state', async () => {
    const os = makeOs()
    otaCheck.set({ checking: true, result: null, msg: 'foo', kind: '' })
    otaInstall.set({ installing: true, pct: 50, state: 'downloading', msg: 'bar', kind: '' })
    otaUpload.set({ uploading: true, pct: 30, msg: 'baz', kind: '' })
    rebooting.set({ active: true, reason: 'test', elapsed: 5, timedOut: false })
    os.selectedFile = new File(['fw'], 'fw.bin')

    const { getByText } = render(UpdateDevMockPanel, { props: { os } })
    await fireEvent.click(getByText('Clear all'))

    expect(get(otaCheck).checking).toBe(false)
    expect(get(otaCheck).msg).toBe('')
    expect(get(otaInstall).installing).toBe(false)
    expect(get(otaInstall).pct).toBe(0)
    expect(get(otaUpload).uploading).toBe(false)
    expect(get(rebooting).active).toBe(false)
    expect(os.selectedFile).toBeNull()
  })

  // ── mockCheck* ─────────────────────────────────────────────────────────────

  it('Checking…: sets otaCheck to checking=true', async () => {
    const os = makeOs()
    const { getByText } = render(UpdateDevMockPanel, { props: { os } })
    await fireEvent.click(getByText('Checking…'))
    expect(get(otaCheck).checking).toBe(true)
    expect(get(otaCheck).msg).toBe('Checking for updates…')
  })

  it('Up to date: sets otaCheck kind=ok', async () => {
    const os = makeOs()
    const { getByText } = render(UpdateDevMockPanel, { props: { os } })
    await fireEvent.click(getByText('Up to date'))
    const s = get(otaCheck)
    expect(s.kind).toBe('ok')
    expect(s.result?.update_available).toBe(false)
    expect(s.msg).toContain('up to date')
  })

  it('Update available: sets otaCheck kind=avail', async () => {
    const os = makeOs()
    const { getByText } = render(UpdateDevMockPanel, { props: { os } })
    await fireEvent.click(getByText('Update available'))
    const s = get(otaCheck)
    expect(s.kind).toBe('avail')
    expect(s.result?.update_available).toBe(true)
    expect(s.result?.latest_version).toBe('v0.99.0-mock')
  })

  it('Check error: sets otaCheck kind=err', async () => {
    const os = makeOs()
    const { getByText } = render(UpdateDevMockPanel, { props: { os } })
    await fireEvent.click(getByText('Check error'))
    const s = get(otaCheck)
    expect(s.kind).toBe('err')
    expect(s.msg).toContain('Failed to check')
  })

  // ── mockInstall* ───────────────────────────────────────────────────────────

  it('Install Starting: sets otaInstall installing=true pct=0', async () => {
    const os = makeOs()
    const { getAllByText } = render(UpdateDevMockPanel, { props: { os } })
    // "Starting" appears in both Install and Upload groups; Install is first
    await fireEvent.click(getAllByText('Starting')[0])
    const s = get(otaInstall)
    expect(s.installing).toBe(true)
    expect(s.pct).toBe(0)
    expect(s.state).toBe('starting')
  })

  it('Install 50%: sets otaInstall pct=50', async () => {
    const os = makeOs()
    const { getAllByText } = render(UpdateDevMockPanel, { props: { os } })
    // "50%" appears in Install group; getAllByText in case it's ambiguous
    const btn = getAllByText('50%')[0]
    await fireEvent.click(btn)
    expect(get(otaInstall).pct).toBe(50)
    expect(get(otaInstall).state).toBe('downloading')
  })

  it('Install 92%: sets otaInstall pct=92', async () => {
    const os = makeOs()
    const { getByText } = render(UpdateDevMockPanel, { props: { os } })
    await fireEvent.click(getByText('92%'))
    expect(get(otaInstall).pct).toBe(92)
    expect(get(otaInstall).state).toBe('writing')
  })

  it('Install Complete: sets otaInstall installing=false kind=ok', async () => {
    const os = makeOs()
    const { getAllByText } = render(UpdateDevMockPanel, { props: { os } })
    // "Complete" appears in both Install and Upload groups; Install is first
    await fireEvent.click(getAllByText('Complete')[0])
    const s = get(otaInstall)
    expect(s.installing).toBe(false)
    expect(s.kind).toBe('ok')
    expect(s.pct).toBe(100)
  })

  it('Install Error: sets otaInstall kind=err', async () => {
    const os = makeOs()
    const { getAllByText } = render(UpdateDevMockPanel, { props: { os } })
    const btn = getAllByText('Error')[0]
    await fireEvent.click(btn)
    const s = get(otaInstall)
    expect(s.kind).toBe('err')
    expect(s.installing).toBe(false)
    expect(s.state).toBe('error')
  })

  // ── mockUpload* ────────────────────────────────────────────────────────────

  it('Upload Starting: sets otaUpload uploading=true pct=0', async () => {
    const os = makeOs()
    // "Starting" text appears in both Install and Upload groups
    const { getAllByText } = render(UpdateDevMockPanel, { props: { os } })
    const btns = getAllByText('Starting')
    // Upload Starting is the second one (after Install Starting)
    await fireEvent.click(btns[1])
    const s = get(otaUpload)
    expect(s.uploading).toBe(true)
    expect(s.pct).toBe(0)
  })

  it('Upload 50%: sets otaUpload pct=50', async () => {
    const os = makeOs()
    const { getAllByText } = render(UpdateDevMockPanel, { props: { os } })
    const btns = getAllByText('50%')
    await fireEvent.click(btns[1])
    expect(get(otaUpload).pct).toBe(50)
    expect(get(otaUpload).uploading).toBe(true)
  })

  it('Upload Complete: sets otaUpload kind=ok', async () => {
    const os = makeOs()
    const { getAllByText } = render(UpdateDevMockPanel, { props: { os } })
    const btns = getAllByText('Complete')
    await fireEvent.click(btns[1])
    const s = get(otaUpload)
    expect(s.uploading).toBe(false)
    expect(s.kind).toBe('ok')
    expect(s.pct).toBe(100)
    expect(s.msg).toContain('Upload complete')
  })

  it('Upload Error: sets otaUpload kind=err', async () => {
    const os = makeOs()
    const { getAllByText } = render(UpdateDevMockPanel, { props: { os } })
    const btns = getAllByText('Error')
    await fireEvent.click(btns[1])
    const s = get(otaUpload)
    expect(s.kind).toBe('err')
    expect(s.uploading).toBe(false)
    expect(s.msg).toContain('Upload failed')
  })

  // ── Reboot ─────────────────────────────────────────────────────────────────

  it('Rebooting on: sets rebooting.active=true', async () => {
    const os = makeOs()
    const { getByText } = render(UpdateDevMockPanel, { props: { os } })
    await fireEvent.click(getByText('Rebooting on'))
    expect(get(rebooting).active).toBe(true)
    expect(get(rebooting).reason).toContain('mock')
  })

  it('Rebooting off: clears rebooting.active', async () => {
    const os = makeOs()
    rebooting.set({ active: true, reason: 'test', elapsed: 3, timedOut: false })
    const { getByText } = render(UpdateDevMockPanel, { props: { os } })
    await fireEvent.click(getByText('Rebooting off'))
    expect(get(rebooting).active).toBe(false)
  })

  // ── Combined ───────────────────────────────────────────────────────────────

  it('Both progress bars: sets both otaInstall and otaUpload uploading/installing', async () => {
    const os = makeOs()
    const { getByText } = render(UpdateDevMockPanel, { props: { os } })
    await fireEvent.click(getByText('Both progress bars'))
    expect(get(otaInstall).installing).toBe(true)
    expect(get(otaInstall).pct).toBe(65)
    expect(get(otaUpload).uploading).toBe(true)
    expect(get(otaUpload).pct).toBe(35)
  })

  it('Install confirm dialog: opens installConfirmOpen and seeds available update', async () => {
    const os = makeOs()
    const { getByText } = render(UpdateDevMockPanel, { props: { os } })
    await fireEvent.click(getByText('Install confirm dialog'))
    expect(os.installConfirmOpen).toBe(true)
    expect(get(otaCheck).result?.update_available).toBe(true)
  })

  it('Install confirm dialog: does not overwrite existing available result', async () => {
    const os = makeOs()
    otaCheck.set({
      checking: false,
      result: { update_available: true, latest_version: 'v9.9.9', current_version: 'v1.0.0' } as any,
      msg: 'Update available',
      kind: 'avail',
    })
    const { getByText } = render(UpdateDevMockPanel, { props: { os } })
    await fireEvent.click(getByText('Install confirm dialog'))
    expect(get(otaCheck).result?.latest_version).toBe('v9.9.9')
  })

  it('Upload confirm dialog: opens uploadConfirmOpen and creates a mock file if none selected', async () => {
    const os = makeOs()
    const { getByText } = render(UpdateDevMockPanel, { props: { os } })
    await fireEvent.click(getByText('Upload confirm dialog'))
    expect(os.uploadConfirmOpen).toBe(true)
    expect(os.selectedFile).not.toBeNull()
    expect(os.selectedFile!.name).toContain('.bin')
  })

  it('Upload confirm dialog: uses existing selectedFile if already set', async () => {
    const os = makeOs()
    const existingFile = new File(['fw'], 'custom.bin')
    os.selectedFile = existingFile
    const { getByText } = render(UpdateDevMockPanel, { props: { os } })
    await fireEvent.click(getByText('Upload confirm dialog'))
    expect(os.selectedFile).toBe(existingFile)
    expect(os.uploadConfirmOpen).toBe(true)
  })

  // ── Update available with $info.version ────────────────────────────────────

  it('Update available uses $info.version in message when info is set', async () => {
    const os = makeOs()
    info.set({ version: 'v1.2.3' } as any)
    const { getByText } = render(UpdateDevMockPanel, { props: { os } })
    await fireEvent.click(getByText('Update available'))
    const s = get(otaCheck)
    expect(s.msg).toContain('v1.2.3')
    expect(s.result?.current_version).toBe('v1.2.3')
  })
})
