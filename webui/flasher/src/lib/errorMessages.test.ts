import { describe, it, expect } from 'vitest'
import {
  isUserCancellation,
  isDeviceLost,
  categorizeConnectError,
  categorizeFlashError,
} from './errorMessages'

describe('isUserCancellation', () => {
  it('returns true for "No port selected by the user"', () => {
    expect(isUserCancellation('No port selected by the user')).toBe(true)
  })

  it('is case-insensitive', () => {
    expect(isUserCancellation('no port selected by the user')).toBe(true)
    expect(isUserCancellation('NO PORT SELECTED BY THE USER')).toBe(true)
  })

  it('returns false for unrelated errors', () => {
    expect(isUserCancellation('Failed to connect')).toBe(false)
    expect(isUserCancellation('')).toBe(false)
  })
})

describe('isDeviceLost', () => {
  it('returns true when deviceDisconnected flag is set', () => {
    expect(isDeviceLost('anything', true)).toBe(true)
  })

  it('returns true for setSignals in message', () => {
    expect(isDeviceLost('setSignals error', false)).toBe(true)
  })

  it('returns true for "Failed to open"', () => {
    expect(isDeviceLost('Failed to open the port', false)).toBe(true)
  })

  it('returns true for "Failed to set"', () => {
    expect(isDeviceLost('Failed to set baud rate', false)).toBe(true)
  })

  it('returns true for "device has been lost"', () => {
    expect(isDeviceLost('The device has been lost', false)).toBe(true)
  })

  it('returns false for unrelated error with flag false', () => {
    expect(isDeviceLost('Unknown error', false)).toBe(false)
  })
})

describe('categorizeConnectError', () => {
  it('returns cancelled for "No port selected"', () => {
    const result = categorizeConnectError('No port selected by the user', false)
    expect(result.kind).toBe('cancelled')
  })

  it('returns device-lost with exact message for disconnect error', () => {
    const result = categorizeConnectError('setSignals failed', false)
    expect(result.kind).toBe('device-lost')
    if (result.kind === 'device-lost') {
      expect(result.message).toBe('Device disconnected — plug it back in and click Connect to retry')
    }
  })

  it('returns device-lost when deviceDisconnected flag is set', () => {
    const result = categorizeConnectError('Unknown error', true)
    expect(result.kind).toBe('device-lost')
  })

  it('returns sync-failed for timeout message', () => {
    const raw = 'Chip sync timed out after 30s — try unplugging and replugging the device'
    const result = categorizeConnectError(raw, false)
    expect(result.kind).toBe('sync-failed')
    if (result.kind === 'sync-failed') {
      expect(result.message).toBe(`${raw}. Put the device into download mode and try again.`)
    }
  })

  it('returns sync-failed for "Failed to connect"', () => {
    const result = categorizeConnectError('Failed to connect to ESP32', false)
    expect(result.kind).toBe('sync-failed')
  })

  it('returns sync-failed for "sync" in message', () => {
    const result = categorizeConnectError('sync error occurred', false)
    expect(result.kind).toBe('sync-failed')
  })

  it('returns unknown for unrecognized error', () => {
    const result = categorizeConnectError('Something totally unexpected', false)
    expect(result.kind).toBe('unknown')
    if (result.kind === 'unknown') {
      expect(result.message).toBe('Something totally unexpected')
    }
  })
})

describe('categorizeFlashError', () => {
  it('returns device-lost message for setSignals error', () => {
    const msg = categorizeFlashError('setSignals failed', false)
    expect(msg).toBe(
      'Device disconnected mid-flash — the firmware on the device is now in an unknown state. Plug it back in, click Connect, and re-flash.'
    )
  })

  it('returns device-lost message when deviceDisconnected flag is set', () => {
    const msg = categorizeFlashError('anything', true)
    expect(msg).toBe(
      'Device disconnected mid-flash — the firmware on the device is now in an unknown state. Plug it back in, click Connect, and re-flash.'
    )
  })

  it('returns raw message for other errors', () => {
    const msg = categorizeFlashError('Write failed: CRC mismatch', false)
    expect(msg).toBe('Write failed: CRC mismatch')
  })
})
