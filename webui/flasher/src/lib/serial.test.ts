import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'

const mocks = vi.hoisted(() => {
  const transportInstance = {
    disconnect: vi.fn().mockResolvedValue(undefined),
  }
  const esploaderInstance = {
    main: vi.fn().mockResolvedValue('ESP32-S3'),
    chip: { readMac: vi.fn().mockResolvedValue('AA:BB:CC:DD:EE:FF') },
    detectFlashSize: vi.fn().mockResolvedValue('4MB'),
    writeFlash: vi.fn().mockResolvedValue(undefined),
    after: vi.fn().mockResolvedValue(undefined),
  }

  function MockTransport() { return transportInstance }
  function MockESPLoader() { return esploaderInstance }

  return { transportInstance, esploaderInstance, MockTransport, MockESPLoader }
})

vi.mock('esptool-js', () => ({
  Transport: mocks.MockTransport,
  ESPLoader: mocks.MockESPLoader,
}))

import { requestPort, connectDevice, disconnectDevice, writeFirmware, hardReset } from './serial'
import type { Transport, ESPLoader } from 'esptool-js'

beforeEach(() => {
  vi.clearAllMocks()
  mocks.transportInstance.disconnect.mockResolvedValue(undefined)
  mocks.esploaderInstance.main.mockResolvedValue('ESP32-S3')
  mocks.esploaderInstance.chip.readMac.mockResolvedValue('AA:BB:CC:DD:EE:FF')
  mocks.esploaderInstance.detectFlashSize.mockResolvedValue('4MB')
  mocks.esploaderInstance.writeFlash.mockResolvedValue(undefined)
  mocks.esploaderInstance.after.mockResolvedValue(undefined)
  ;(globalThis as unknown as { navigator: { serial: { requestPort: ReturnType<typeof vi.fn> } } })
    .navigator.serial.requestPort = vi.fn().mockResolvedValue({ id: 'mock-port' })
})

afterEach(() => {
  vi.restoreAllMocks()
})

describe('requestPort', () => {
  it('wraps navigator.serial.requestPort and returns the port', async () => {
    const port = await requestPort()
    expect(port).toEqual({ id: 'mock-port' })
  })

  it('propagates rejection from navigator.serial.requestPort', async () => {
    ;(globalThis as unknown as { navigator: { serial: { requestPort: ReturnType<typeof vi.fn> } } })
      .navigator.serial.requestPort = vi.fn().mockRejectedValue(new Error('No port selected by the user'))
    await expect(requestPort()).rejects.toThrow('No port selected by the user')
  })
})

describe('connectDevice', () => {
  it('returns ConnectedDevice with ChipInfo on success', async () => {
    const port = { id: 'mock-port' }
    const device = await connectDevice(port)
    expect(device.chipInfo.chip).toBe('ESP32-S3')
    expect(device.chipInfo.mac).toBe('AA:BB:CC:DD:EE:FF')
    expect(device.chipInfo.flashSize).toBe('4MB')
    expect(device.port).toBe(port)
  })

  it('falls back to "unknown" flash size if detectFlashSize throws', async () => {
    mocks.esploaderInstance.detectFlashSize.mockRejectedValueOnce(new Error('detect failed'))
    const device = await connectDevice({})
    expect(device.chipInfo.flashSize).toBe('unknown')
  })

  it('throws if chip sync (main) fails', async () => {
    mocks.esploaderInstance.main.mockRejectedValueOnce(new Error('Chip sync timed out'))
    await expect(connectDevice({})).rejects.toThrow('Chip sync timed out')
  })
})

describe('disconnectDevice', () => {
  it('calls disconnect on transport', async () => {
    await disconnectDevice(mocks.transportInstance as unknown as Transport)
    expect(mocks.transportInstance.disconnect).toHaveBeenCalled()
  })

  it('swallows errors from disconnect', async () => {
    mocks.transportInstance.disconnect.mockRejectedValueOnce(new Error('disconnect error'))
    await expect(disconnectDevice(mocks.transportInstance as unknown as Transport)).resolves.toBeUndefined()
  })

  it('handles null transport gracefully', async () => {
    await expect(disconnectDevice(null)).resolves.toBeUndefined()
  })
})

describe('writeFirmware', () => {
  it('calls writeFlash with correct parameters and passes progress', async () => {
    const bin = new Uint8Array([1, 2, 3])
    const onProgress = vi.fn()

    mocks.esploaderInstance.writeFlash.mockImplementationOnce(async (opts: { reportProgress: Function }) => {
      opts.reportProgress(0, 1, 3)
      opts.reportProgress(0, 3, 3)
    })

    await writeFirmware(mocks.esploaderInstance as unknown as ESPLoader, bin, onProgress)

    expect(mocks.esploaderInstance.writeFlash).toHaveBeenCalledWith(
      expect.objectContaining({
        fileArray: [{ data: bin, address: 0x0 }],
        flashSize: 'keep',
        flashMode: 'keep',
        flashFreq: 'keep',
        eraseAll: false,
        compress: true,
      })
    )
    expect(onProgress).toHaveBeenCalledWith(1, 3)
    expect(onProgress).toHaveBeenCalledWith(3, 3)
  })
})

describe('hardReset', () => {
  it('calls esploader.after with hard_reset and usbOtg flag', async () => {
    await hardReset(mocks.esploaderInstance as unknown as ESPLoader, true)
    expect(mocks.esploaderInstance.after).toHaveBeenCalledWith('hard_reset', true)
  })

  it('swallows errors from after (best-effort)', async () => {
    mocks.esploaderInstance.after.mockRejectedValueOnce(new Error('reset error'))
    await expect(hardReset(mocks.esploaderInstance as unknown as ESPLoader, false)).resolves.toBeUndefined()
  })
})
