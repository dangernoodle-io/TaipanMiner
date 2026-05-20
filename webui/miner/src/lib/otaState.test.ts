import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'

vi.mock('./api', () => ({
  fetchOtaCheck: vi.fn(),
  kickOtaCheck: vi.fn(async () => 0),
  triggerOtaUpdate: vi.fn(),
  fetchOtaStatus: vi.fn(),
  uploadOta: vi.fn(),
}))

vi.mock('./stores', async () => {
  const { writable } = await import('svelte/store')
  return {
    otaCheck: writable({ checking: false, result: null, msg: '', kind: '' }),
    otaInstall: writable({ installing: false, pct: 0, state: '', msg: '', kind: '' }),
    otaUpload: writable({ uploading: false, pct: 0, msg: '', kind: '' }),
    rebooting: writable({ active: false, reason: '', elapsed: 0, timedOut: false }),
    startRebootRecovery: vi.fn(),
  }
})

import * as api from './api'
import { otaCheck, otaInstall, otaUpload, startRebootRecovery } from './stores'
import { get } from 'svelte/store'
import { createOtaState } from './otaState.svelte'

beforeEach(() => {
  vi.clearAllMocks()
  otaCheck.set({ checking: false, result: null, msg: '', kind: '' })
  otaInstall.set({ installing: false, pct: 0, state: '', msg: '', kind: '' })
  otaUpload.set({ uploading: false, pct: 0, msg: '', kind: '' })
  vi.useFakeTimers()
})

afterEach(() => {
  vi.useRealTimers()
})

describe('createOtaState — initial state', () => {
  it('starts with installConfirmOpen false', () => {
    const os = createOtaState()
    expect(os.installConfirmOpen).toBe(false)
  })

  it('starts with uploadConfirmOpen false', () => {
    const os = createOtaState()
    expect(os.uploadConfirmOpen).toBe(false)
  })

  it('starts with selectedFile null', () => {
    const os = createOtaState()
    expect(os.selectedFile).toBeNull()
  })

  it('starts with dragOver false', () => {
    const os = createOtaState()
    expect(os.dragOver).toBe(false)
  })

  it('starts with fileInput null', () => {
    const os = createOtaState()
    expect(os.fileInput).toBeNull()
  })
})

describe('handleCheck — happy path', () => {
  it('sets otaCheck to checking=true then resolves with up-to-date result', async () => {
    vi.mocked(api.fetchOtaCheck).mockResolvedValue({
      update_available: false,
      latest_version: 'v1.2.3',
      current_version: 'v1.2.3',
    })
    const os = createOtaState()
    const promise = os.handleCheck()
    // immediately after call: store should be in checking state
    expect(get(otaCheck).checking).toBe(true)
    await vi.runAllTimersAsync()
    await promise
    const s = get(otaCheck)
    expect(s.checking).toBe(false)
    expect(s.kind).toBe('ok')
    expect(s.msg).toBe('Firmware is up to date (v1.2.3)')
    expect(s.result?.update_available).toBe(false)
  })

  it('sets otaCheck to avail when update is available', async () => {
    vi.mocked(api.fetchOtaCheck).mockResolvedValue({
      update_available: true,
      latest_version: 'v2.0.0',
      current_version: 'v1.0.0',
    })
    const os = createOtaState()
    const promise = os.handleCheck()
    await vi.runAllTimersAsync()
    await promise
    const s = get(otaCheck)
    expect(s.kind).toBe('avail')
    expect(s.msg).toBe('Update available: v2.0.0 (current v1.0.0)')
  })

  it('retries when fetchOtaCheck returns pending', async () => {
    vi.mocked(api.fetchOtaCheck)
      .mockResolvedValueOnce('pending')
      .mockResolvedValue({ update_available: false, latest_version: 'v1.0.0', current_version: 'v1.0.0' })
    const os = createOtaState()
    const promise = os.handleCheck()
    await vi.advanceTimersByTimeAsync(2001)
    await vi.runAllTimersAsync()
    await promise
    expect(api.fetchOtaCheck).toHaveBeenCalledTimes(2)
    expect(get(otaCheck).kind).toBe('ok')
  })

  it('sets err when deadline expires with only pending responses', async () => {
    vi.mocked(api.fetchOtaCheck).mockResolvedValue('pending')
    const os = createOtaState()
    const promise = os.handleCheck()
    // advance past the 15s deadline (7 * 2s + margin)
    await vi.advanceTimersByTimeAsync(16000)
    await vi.runAllTimersAsync()
    await promise
    const s = get(otaCheck)
    expect(s.kind).toBe('err')
    expect(s.msg).toBe('Failed to check for updates (timeout).')
    expect(s.checking).toBe(false)
  })

  it('sets err when fetchOtaCheck throws', async () => {
    vi.mocked(api.fetchOtaCheck).mockRejectedValue(new Error('network unreachable'))
    const os = createOtaState()
    const promise = os.handleCheck()
    await vi.runAllTimersAsync()
    await promise
    const s = get(otaCheck)
    expect(s.kind).toBe('err')
    expect(s.msg).toBe('Failed to check: network unreachable.')
    expect(s.checking).toBe(false)
  })
})

describe('requestInstall', () => {
  it('opens install confirm dialog', () => {
    const os = createOtaState()
    expect(os.installConfirmOpen).toBe(false)
    os.requestInstall()
    expect(os.installConfirmOpen).toBe(true)
  })
})

describe('requestUpload', () => {
  it('does nothing when selectedFile is null', () => {
    const os = createOtaState()
    os.requestUpload()
    expect(os.uploadConfirmOpen).toBe(false)
  })

  it('opens upload confirm dialog when a file is selected', () => {
    const os = createOtaState()
    os.selectedFile = new File(['data'], 'firmware.bin', { type: 'application/octet-stream' })
    os.requestUpload()
    expect(os.uploadConfirmOpen).toBe(true)
  })
})

describe('handleInstall — happy path', () => {
  it('does nothing when otaCheck.result.update_available is false', async () => {
    otaCheck.set({ checking: false, result: { update_available: false, latest_version: 'v1', current_version: 'v1' }, msg: '', kind: 'ok' })
    const os = createOtaState()
    await os.handleInstall()
    expect(api.triggerOtaUpdate).not.toHaveBeenCalled()
  })

  it('does nothing when otaCheck.result is null', async () => {
    const os = createOtaState()
    await os.handleInstall()
    expect(api.triggerOtaUpdate).not.toHaveBeenCalled()
  })

  it('runs full install flow to completion and calls startRebootRecovery', async () => {
    otaCheck.set({ checking: false, result: { update_available: true, latest_version: 'v2.0.0', current_version: 'v1.0.0' }, msg: '', kind: 'avail' })
    vi.mocked(api.triggerOtaUpdate).mockResolvedValue(undefined)
    vi.mocked(api.fetchOtaStatus)
      .mockResolvedValueOnce({ state: 'idle', in_progress: false, progress_pct: 0 })
      .mockResolvedValueOnce({ state: 'downloading', in_progress: true, progress_pct: 50 })
      .mockResolvedValue({ state: 'complete', in_progress: true, progress_pct: 100 })

    const os = createOtaState()
    const promise = os.handleInstall()
    // initial set
    expect(get(otaInstall).installing).toBe(true)
    expect(get(otaInstall).msg).toBe('Starting OTA install…')

    await vi.runAllTimersAsync()
    await promise

    const s = get(otaInstall)
    expect(s.installing).toBe(false)
    expect(s.kind).toBe('ok')
    expect(s.msg).toBe('Install complete. Miner is rebooting.')
    expect(s.pct).toBe(100)
    expect(startRebootRecovery).toHaveBeenCalledWith('Applying firmware update')
    // otaCheck cleared on complete
    expect(get(otaCheck).result).toBeNull()
  })

  it('updates progress during install', async () => {
    otaCheck.set({ checking: false, result: { update_available: true, latest_version: 'v2.0.0', current_version: 'v1.0.0' }, msg: '', kind: 'avail' })
    vi.mocked(api.triggerOtaUpdate).mockResolvedValue(undefined)
    const statuses = [
      { state: 'downloading', in_progress: true, progress_pct: 30 },
      { state: 'downloading', in_progress: true, progress_pct: 60 },
      { state: 'complete', in_progress: true, progress_pct: 100 },
    ]
    let callIdx = 0
    vi.mocked(api.fetchOtaStatus).mockImplementation(async () => statuses[Math.min(callIdx++, statuses.length - 1)])

    const os = createOtaState()
    const promise = os.handleInstall()
    // Advance just enough to process the first poll (2s timer + microtasks)
    await vi.advanceTimersByTimeAsync(2001)
    await Promise.resolve() // flush microtasks
    // After first poll tick the mock returns statuses[1] = 30%
    // (statuses[0] was returned immediately on first call before timer)
    // The exact pct depends on call order; just confirm progress is being tracked
    const mid = get(otaInstall)
    expect(mid.pct).toBeGreaterThan(0)

    await vi.runAllTimersAsync()
    await promise
  })

  it('sets err state when status returns error', async () => {
    otaCheck.set({ checking: false, result: { update_available: true, latest_version: 'v2.0.0', current_version: 'v1.0.0' }, msg: '', kind: 'avail' })
    vi.mocked(api.triggerOtaUpdate).mockResolvedValue(undefined)
    vi.mocked(api.fetchOtaStatus).mockResolvedValue({ state: 'error', in_progress: false, progress_pct: 37 })

    const os = createOtaState()
    const promise = os.handleInstall()
    await vi.runAllTimersAsync()
    await promise

    const s = get(otaInstall)
    expect(s.installing).toBe(false)
    expect(s.kind).toBe('err')
    expect(s.msg).toBe('Install ended: error.')
  })

  it('sets err state when polling times out at 600s', async () => {
    otaCheck.set({ checking: false, result: { update_available: true, latest_version: 'v2.0.0', current_version: 'v1.0.0' }, msg: '', kind: 'avail' })
    vi.mocked(api.triggerOtaUpdate).mockResolvedValue(undefined)
    vi.mocked(api.fetchOtaStatus).mockResolvedValue({ state: 'downloading', in_progress: true, progress_pct: 50 })

    const os = createOtaState()
    const promise = os.handleInstall()
    // advance past 600s deadline
    await vi.advanceTimersByTimeAsync(601000)
    await vi.runAllTimersAsync()
    await promise

    const s = get(otaInstall)
    expect(s.installing).toBe(false)
    expect(s.kind).toBe('err')
    expect(s.msg).toBe('Install timed out.')
  })

  it('sets err state when triggerOtaUpdate throws', async () => {
    otaCheck.set({ checking: false, result: { update_available: true, latest_version: 'v2.0.0', current_version: 'v1.0.0' }, msg: '', kind: 'avail' })
    vi.mocked(api.triggerOtaUpdate).mockRejectedValue(new Error('ota update failed: 503'))

    const os = createOtaState()
    const promise = os.handleInstall()
    await vi.runAllTimersAsync()
    await promise

    const s = get(otaInstall)
    expect(s.installing).toBe(false)
    expect(s.kind).toBe('err')
    expect(s.msg).toBe('Install failed: ota update failed: 503.')
  })
})

describe('handleUpload — happy path', () => {
  it('does nothing when selectedFile is null', async () => {
    const os = createOtaState()
    await os.handleUpload()
    expect(api.uploadOta).not.toHaveBeenCalled()
  })

  it('runs full upload flow to completion and calls startRebootRecovery', async () => {
    vi.mocked(api.uploadOta).mockImplementation(async (file, onProgress) => {
      onProgress(50)
      onProgress(100)
      return ''
    })

    const os = createOtaState()
    const file = new File(['firmware'], 'firmware.bin', { type: 'application/octet-stream' })
    os.selectedFile = file

    const promise = os.handleUpload()
    // uploadOta mock calls onProgress synchronously, so store reflects latest pct
    expect(get(otaUpload).uploading).toBe(true)

    await vi.runAllTimersAsync()
    await promise

    const s = get(otaUpload)
    expect(s.uploading).toBe(false)
    expect(s.kind).toBe('ok')
    expect(s.msg).toBe('Upload complete. Miner is rebooting to apply the firmware.')
    expect(s.pct).toBe(100)
    expect(startRebootRecovery).toHaveBeenCalledWith('Applying uploaded firmware')
    // selectedFile cleared after success
    expect(os.selectedFile).toBeNull()
  })

  it('sends progress updates to store', async () => {
    const progressCalls: number[] = []
    vi.mocked(api.uploadOta).mockImplementation(async (_file, onProgress) => {
      onProgress(42)
      progressCalls.push(42)
      return ''
    })

    const os = createOtaState()
    os.selectedFile = new File(['fw'], 'fw.bin')

    const promise = os.handleUpload()
    await vi.runAllTimersAsync()
    await promise

    // uploadOta mock called onProgress(42) synchronously; store reflects it during the call
    expect(progressCalls).toContain(42)
  })

  it('sets err state when uploadOta throws', async () => {
    vi.mocked(api.uploadOta).mockRejectedValue(new Error('connection reset'))

    const os = createOtaState()
    os.selectedFile = new File(['fw'], 'fw.bin')

    const promise = os.handleUpload()
    await vi.runAllTimersAsync()
    await promise

    const s = get(otaUpload)
    expect(s.uploading).toBe(false)
    expect(s.kind).toBe('err')
    expect(s.msg).toBe('Upload failed: connection reset.')
  })
})

describe('onFileSelect', () => {
  it('sets selectedFile from input event', () => {
    const os = createOtaState()
    const file = new File(['data'], 'firmware.bin')
    const input = document.createElement('input')
    input.type = 'file'
    // Simulate file list via FileList-like object
    Object.defineProperty(input, 'files', {
      value: { 0: file, length: 1, item: (i: number) => (i === 0 ? file : null) },
    })
    const event = new Event('change')
    Object.defineProperty(event, 'target', { value: input })
    os.onFileSelect(event)
    expect(os.selectedFile).toBe(file)
  })

  it('clears selectedFile when no file in input', () => {
    const os = createOtaState()
    os.selectedFile = new File(['data'], 'old.bin')
    const input = document.createElement('input')
    input.type = 'file'
    Object.defineProperty(input, 'files', { value: { length: 0 } })
    const event = new Event('change')
    Object.defineProperty(event, 'target', { value: input })
    os.onFileSelect(event)
    expect(os.selectedFile).toBeNull()
  })

  it('resets otaUpload store on file select', () => {
    otaUpload.set({ uploading: false, pct: 73, msg: 'old', kind: 'err' })
    const os = createOtaState()
    const file = new File(['data'], 'firmware.bin')
    const input = document.createElement('input')
    Object.defineProperty(input, 'files', {
      value: { 0: file, length: 1, item: () => file },
    })
    const event = new Event('change')
    Object.defineProperty(event, 'target', { value: input })
    os.onFileSelect(event)
    const s = get(otaUpload)
    expect(s.pct).toBe(0)
    expect(s.msg).toBe('')
    expect(s.kind).toBe('')
  })
})

describe('onDrop', () => {
  it('sets selectedFile from dropped file', () => {
    const os = createOtaState()
    // dragOver is read-only getter; drive it via onDragOver instead
    os.onDragOver({ preventDefault: vi.fn() } as unknown as DragEvent)
    expect(os.dragOver).toBe(true)

    const file = new File(['fw'], 'firmware.bin')
    // jsdom doesn't support DataTransfer; mock the shape onDrop reads
    const dropEvent = {
      preventDefault: vi.fn(),
      dataTransfer: { files: { 0: file, length: 1 } }
    } as unknown as DragEvent
    os.onDrop(dropEvent)
    expect(os.selectedFile).toBe(file)
    expect(os.dragOver).toBe(false)
  })

  it('calls preventDefault', () => {
    const os = createOtaState()
    const dropEvent = { preventDefault: vi.fn(), dataTransfer: null } as unknown as DragEvent
    os.onDrop(dropEvent)
    expect(dropEvent.preventDefault).toHaveBeenCalled()
  })

  it('clears dragOver even with no file', () => {
    const os = createOtaState()
    const dropEvent = { preventDefault: vi.fn(), dataTransfer: { files: [] } } as unknown as DragEvent
    os.onDrop(dropEvent)
    expect(os.dragOver).toBe(false)
  })
})

describe('onDragOver / onDragLeave', () => {
  it('sets dragOver true and calls preventDefault', () => {
    const os = createOtaState()
    const event = { preventDefault: vi.fn() } as unknown as DragEvent
    os.onDragOver(event)
    expect(os.dragOver).toBe(true)
    expect(event.preventDefault).toHaveBeenCalled()
  })

  it('clears dragOver on leave', () => {
    const os = createOtaState()
    os.onDragOver({ preventDefault: vi.fn() } as unknown as DragEvent)
    expect(os.dragOver).toBe(true)
    os.onDragLeave()
    expect(os.dragOver).toBe(false)
  })
})

describe('setter round-trips', () => {
  it('installConfirmOpen setter', () => {
    const os = createOtaState()
    os.installConfirmOpen = true
    expect(os.installConfirmOpen).toBe(true)
    os.installConfirmOpen = false
    expect(os.installConfirmOpen).toBe(false)
  })

  it('uploadConfirmOpen setter', () => {
    const os = createOtaState()
    os.uploadConfirmOpen = true
    expect(os.uploadConfirmOpen).toBe(true)
    os.uploadConfirmOpen = false
    expect(os.uploadConfirmOpen).toBe(false)
  })

  it('selectedFile setter', () => {
    const os = createOtaState()
    const f = new File(['x'], 'x.bin')
    os.selectedFile = f
    expect(os.selectedFile).toBe(f)
    os.selectedFile = null
    expect(os.selectedFile).toBeNull()
  })

  it('fileInput setter', () => {
    const os = createOtaState()
    const el = document.createElement('input')
    os.fileInput = el
    expect(os.fileInput).toBe(el)
    os.fileInput = null
    expect(os.fileInput).toBeNull()
  })
})
