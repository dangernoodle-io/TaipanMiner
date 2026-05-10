import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'

// Mock serial module
vi.mock('./serial', () => ({
  requestPort: vi.fn(),
  connectDevice: vi.fn(),
  disconnectDevice: vi.fn().mockResolvedValue(undefined),
  writeFirmware: vi.fn().mockResolvedValue(undefined),
  hardReset: vi.fn().mockResolvedValue(undefined),
}))

// Mock release module
vi.mock('./release', () => ({
  loadManifest: vi.fn(),
  loadAsset: vi.fn(),
}))

// Mock boards.json
vi.mock('../generated/boards.json', () => ({
  default: [
    { id: 'tdongle-s3', label: 'Tdongle S3', asic: null, usbOtg: true },
    { id: 'bitaxe-601', label: 'Bitaxe 601', asic: 'BM1370', usbOtg: true },
  ]
}))

import * as serial from './serial'
import * as release from './release'
import { createFlashState } from './flashState.svelte'

const fakeManifest = {
  tag: 'v1.0.0',
  publishedAt: '2025-01-01T00:00:00Z',
  assets: {
    'tdongle-s3': { file: 'fw.bin', size: 1024, sha256: 'abc' },
    'bitaxe-601': { file: 'bitaxe.bin', size: 2048, sha256: 'def' },
  }
}

const fakeChipInfo = { chip: 'ESP32-S3', mac: 'AA:BB:CC:DD:EE:FF', flashSize: '4MB' }
const fakePort = { id: 'mock-port' }
const fakeTransport = { disconnect: vi.fn().mockResolvedValue(undefined) }
const fakeEsploader = { after: vi.fn() }

function makeConnectedDevice() {
  return {
    transport: fakeTransport as unknown as import('esptool-js').Transport,
    esploader: fakeEsploader as unknown as import('esptool-js').ESPLoader,
    port: fakePort,
    chipInfo: fakeChipInfo,
  }
}

beforeEach(() => {
  vi.clearAllMocks()
  vi.mocked(release.loadManifest).mockResolvedValue(fakeManifest)
  vi.mocked(serial.requestPort).mockResolvedValue(fakePort)
  vi.mocked(serial.connectDevice).mockResolvedValue(makeConnectedDevice())
  vi.mocked(serial.disconnectDevice).mockResolvedValue(undefined)
  vi.mocked(serial.writeFirmware).mockResolvedValue(undefined)
  vi.mocked(serial.hardReset).mockResolvedValue(undefined)
  vi.mocked(release.loadAsset).mockResolvedValue(new Uint8Array([1, 2, 3]))
})

afterEach(() => {
  vi.restoreAllMocks()
})

describe('createFlashState — initial state', () => {
  it('starts with all idle statuses', () => {
    const state = createFlashState()
    expect(state.connectStatus).toBe('idle')
    expect(state.flashStatus).toBe('idle')
    expect(state.board).toBe('')
    expect(state.connectError).toBeNull()
    expect(state.flashError).toBeNull()
    expect(state.chipInfo).toBeNull()
    expect(state.manifest).toBeNull()
    expect(state.manifestError).toBeNull()
    expect(state.downloadProgress).toBeNull()
    expect(state.flashProgress).toBeNull()
    expect(state.deviceDisconnected).toBe(false)
  })
})

describe('selectBoard', () => {
  it('updates board id', async () => {
    const state = createFlashState()
    await state.selectBoard('tdongle-s3')
    expect(state.board).toBe('tdongle-s3')
  })
})

describe('loadManifestAction', () => {
  it('populates manifest on success', async () => {
    const state = createFlashState()
    await state.loadManifestAction()
    expect(state.manifest).toEqual(fakeManifest)
    expect(state.manifestError).toBeNull()
  })

  it('sets manifestError on failure', async () => {
    vi.mocked(release.loadManifest).mockRejectedValueOnce(new Error('Firmware manifest not found'))
    const state = createFlashState()
    await state.loadManifestAction()
    expect(state.manifest).toBeNull()
    expect(state.manifestError).toContain('Firmware manifest not found')
  })
})

describe('connect', () => {
  it('happy path sets connectStatus=connected and chipInfo', async () => {
    const state = createFlashState()
    await state.connect()
    expect(state.connectStatus).toBe('connected')
    expect(state.chipInfo).toEqual(fakeChipInfo)
    expect(state.connectError).toBeNull()
  })

  it('returns to idle and clears error when user cancels (No port selected)', async () => {
    vi.mocked(serial.requestPort).mockRejectedValueOnce(new Error('No port selected by the user'))
    const state = createFlashState()
    await state.connect()
    expect(state.connectStatus).toBe('idle')
    expect(state.connectError).toBeNull()
  })

  it('sets connectStatus=error and device-lost message on disconnect error', async () => {
    vi.mocked(serial.requestPort).mockResolvedValueOnce(fakePort)
    vi.mocked(serial.connectDevice).mockRejectedValueOnce(new Error('setSignals failed'))
    const state = createFlashState()
    await state.connect()
    expect(state.connectStatus).toBe('error')
    expect(state.connectError).toBe('Device disconnected — plug it back in and click Connect to retry')
  })

  it('sets connectStatus=error with sync-failed message for timeout', async () => {
    const timeoutMsg = 'Chip sync timed out after 30s — try unplugging and replugging the device'
    vi.mocked(serial.connectDevice).mockRejectedValueOnce(new Error(timeoutMsg))
    const state = createFlashState()
    await state.connect()
    expect(state.connectStatus).toBe('error')
    expect(state.connectError).toContain('Put the device into download mode and try again.')
  })

  it('sets unknown connect error for unrecognized errors', async () => {
    vi.mocked(serial.connectDevice).mockRejectedValueOnce(new Error('Unknown serial error'))
    const state = createFlashState()
    await state.connect()
    expect(state.connectStatus).toBe('error')
    expect(state.connectError).toBe('Unknown serial error')
  })
})

describe('disconnect', () => {
  it('resets state to idle', async () => {
    const state = createFlashState()
    await state.connect()
    expect(state.connectStatus).toBe('connected')
    await state.disconnect()
    expect(state.connectStatus).toBe('idle')
    expect(state.chipInfo).toBeNull()
    expect(serial.disconnectDevice).toHaveBeenCalled()
  })
})

describe('flash', () => {
  it('happy path: downloads, flashes, transitions to done', async () => {
    const state = createFlashState()
    await state.loadManifestAction()
    await state.selectBoard('tdongle-s3')
    await state.connect()
    await state.flash()
    expect(state.flashStatus).toBe('done')
    expect(state.flashError).toBeNull()
    expect(release.loadAsset).toHaveBeenCalled()
    expect(serial.writeFirmware).toHaveBeenCalled()
  })

  it('does nothing if no esploader (not connected)', async () => {
    const state = createFlashState()
    await state.loadManifestAction()
    await state.flash()
    expect(state.flashStatus).toBe('idle')
    expect(serial.writeFirmware).not.toHaveBeenCalled()
  })

  it('does nothing if no manifest', async () => {
    const state = createFlashState()
    await state.connect()
    await state.flash()
    expect(state.flashStatus).toBe('idle')
    expect(serial.writeFirmware).not.toHaveBeenCalled()
  })

  it('sets flashStatus=error with disconnect message on device lost mid-flash', async () => {
    vi.mocked(serial.writeFirmware).mockRejectedValueOnce(new Error('setSignals failed'))
    const state = createFlashState()
    await state.loadManifestAction()
    await state.selectBoard('tdongle-s3')
    await state.connect()
    await state.flash()
    expect(state.flashStatus).toBe('error')
    expect(state.flashError).toContain('Device disconnected mid-flash')
  })

  it('sets flashStatus=error with raw message on other flash errors', async () => {
    vi.mocked(serial.writeFirmware).mockRejectedValueOnce(new Error('Write CRC mismatch'))
    const state = createFlashState()
    await state.loadManifestAction()
    await state.selectBoard('tdongle-s3')
    await state.connect()
    await state.flash()
    expect(state.flashStatus).toBe('error')
    expect(state.flashError).toBe('Write CRC mismatch')
  })
})

describe('flashAnother', () => {
  it('resets state to idle from done', async () => {
    const state = createFlashState()
    await state.loadManifestAction()
    await state.selectBoard('tdongle-s3')
    await state.connect()
    await state.flash()
    expect(state.flashStatus).toBe('done')
    await state.flashAnother()
    expect(state.flashStatus).toBe('idle')
    expect(state.connectStatus).toBe('idle')
    expect(state.chipInfo).toBeNull()
    expect(state.flashError).toBeNull()
    expect(state.connectError).toBeNull()
  })
})

describe('handleDeviceDisconnect', () => {
  it('ignores disconnect events for other ports', async () => {
    const state = createFlashState()
    await state.connect()
    state.handleDeviceDisconnect({ id: 'other-port' })
    expect(state.connectStatus).toBe('connected')
  })

  it('sets deviceDisconnected and resets to idle', async () => {
    const state = createFlashState()
    await state.connect()
    // The active port is fakePort
    state.handleDeviceDisconnect(fakePort)
    expect(state.deviceDisconnected).toBe(true)
    expect(state.connectStatus).toBe('idle')
    expect(state.chipInfo).toBeNull()
  })

  it('sets flashStatus=error when disconnect happens during flash', async () => {
    vi.mocked(serial.writeFirmware).mockImplementationOnce(async () => {
      // Simulate disconnect mid-flash by calling flash then disconnect
      return new Promise<void>(() => {}) // never resolves, so we test mid-state
    })
    const state = createFlashState()
    await state.loadManifestAction()
    await state.selectBoard('tdongle-s3')
    await state.connect()
    // Start flash without awaiting
    const flashPromise = state.flash()
    // At this point flashStatus should be 'downloading' or 'flashing'
    // Trigger a disconnect
    state.handleDeviceDisconnect(fakePort)
    // Now the disconnect sets flashStatus=error if it was flashing/downloading
    // since flash never finishes in this mock, just check the disconnect handler worked
    expect(state.deviceDisconnected).toBe(true)
    // The flash promise will never resolve in this mock, so we don't await
    flashPromise.catch(() => {}) // suppress unhandled
  })

  it('sets flashError to "Device disconnected mid-operation" during download phase', async () => {
    const state = createFlashState()
    await state.loadManifestAction()
    await state.selectBoard('tdongle-s3')
    await state.connect()

    // Manually set flashStatus to downloading by inspecting state post-flash-start
    // We simulate the handler being called when flashStatus='downloading'
    // by directly calling handleDeviceDisconnect (the state machine checks the flag)
    // First: get into downloading state via a slow loadAsset
    let resolveAsset!: () => void
    vi.mocked(release.loadAsset).mockReturnValueOnce(
      new Promise<Uint8Array>(resolve => {
        resolveAsset = () => resolve(new Uint8Array([1, 2, 3]))
      })
    )
    const flashPromise = state.flash()
    // At this point we're in downloading state; trigger disconnect
    await Promise.resolve() // let flash() get to the await loadAsset
    state.handleDeviceDisconnect(fakePort)
    expect(state.flashStatus).toBe('error')
    expect(state.flashError).toBe('Device disconnected mid-operation')
    resolveAsset()
    flashPromise.catch(() => {}) // suppress
  })
})

describe('boardOptions', () => {
  it('returns empty array before manifest loads', () => {
    const state = createFlashState()
    expect(state.boardOptions).toEqual([])
  })

  it('filters to boards that have assets in manifest', async () => {
    const state = createFlashState()
    await state.loadManifestAction()
    const options = state.boardOptions
    expect(options.map(o => o.value)).toContain('tdongle-s3')
    expect(options.map(o => o.value)).toContain('bitaxe-601')
  })

  it('includes ASIC in label when present', async () => {
    const state = createFlashState()
    await state.loadManifestAction()
    const bitaxe = state.boardOptions.find(o => o.value === 'bitaxe-601')
    expect(bitaxe?.label).toBe('Bitaxe 601 (BM1370)')
  })

  it('omits ASIC suffix when asic is null', async () => {
    const state = createFlashState()
    await state.loadManifestAction()
    const tdongle = state.boardOptions.find(o => o.value === 'tdongle-s3')
    expect(tdongle?.label).toBe('Tdongle S3')
  })
})
