export function isUserCancellation(raw: string): boolean {
  return /No port selected by the user/i.test(raw)
}

export function isDeviceLost(raw: string, deviceDisconnected: boolean): boolean {
  return deviceDisconnected || /setSignals|Failed to (open|set)|device has been lost/i.test(raw)
}

export type ConnectErrorResult =
  | { kind: 'cancelled' }
  | { kind: 'device-lost'; message: string }
  | { kind: 'sync-failed'; message: string }
  | { kind: 'unknown'; message: string }

export function categorizeConnectError(raw: string, deviceDisconnected: boolean): ConnectErrorResult {
  if (isUserCancellation(raw)) {
    return { kind: 'cancelled' }
  }
  if (isDeviceLost(raw, deviceDisconnected)) {
    return {
      kind: 'device-lost',
      message: 'Device disconnected — plug it back in and click Connect to retry',
    }
  }
  if (/timed out|sync|Failed to connect/i.test(raw)) {
    return {
      kind: 'sync-failed',
      message: `${raw}. Put the device into download mode and try again.`,
    }
  }
  return { kind: 'unknown', message: raw }
}

export function categorizeFlashError(raw: string, deviceDisconnected: boolean): string {
  if (isDeviceLost(raw, deviceDisconnected)) {
    return 'Device disconnected mid-flash — the firmware on the device is now in an unknown state. Plug it back in, click Connect, and re-flash.'
  }
  return raw
}
