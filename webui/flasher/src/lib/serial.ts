import { ESPLoader, Transport } from 'esptool-js'
import { withTimeout } from './withTimeout'

export type ChipInfo = { chip: string; mac: string; flashSize: string }
export type ConnectedDevice = { transport: Transport; esploader: ESPLoader; port: unknown; chipInfo: ChipInfo }

const CONNECT_TIMEOUT_MS = 30_000

export async function requestPort(): Promise<unknown> {
  // @ts-expect-error — Web Serial types may not be in lib.dom for older TS targets
  return navigator.serial.requestPort({})
}

export async function connectDevice(port: unknown): Promise<ConnectedDevice> {
  const t = new Transport(port, true)
  const loader = new ESPLoader({
    transport: t,
    baudrate: 115200,
    terminal: {
      clean: () => {},
      writeLine: (line: string) => console.log('[esptool]', line),
      write: (chunk: string) => console.log('[esptool]', chunk)
    }
  })
  const chip = await withTimeout(loader.main(), CONNECT_TIMEOUT_MS, 'Chip sync')
  const mac = await withTimeout(loader.chip.readMac(loader), 5_000, 'MAC read')
  let flashSize = 'unknown'
  try {
    flashSize = await withTimeout(loader.detectFlashSize(), 5_000, 'Flash detect')
  } catch {
    // best-effort
  }
  return { transport: t, esploader: loader, port, chipInfo: { chip, mac, flashSize } }
}

export async function disconnectDevice(transport: Transport | null): Promise<void> {
  try {
    await transport?.disconnect()
  } catch {
    // ignore
  }
}

export async function writeFirmware(
  esploader: ESPLoader,
  bin: Uint8Array,
  onProgress: (written: number, total: number) => void
): Promise<void> {
  await esploader.writeFlash({
    fileArray: [{ data: bin, address: 0x0 }],
    flashSize: 'keep',
    flashMode: 'keep',
    flashFreq: 'keep',
    eraseAll: false,
    compress: true,
    reportProgress: (_idx: number, written: number, total: number) => {
      onProgress(written, total)
    },
  })
}

export async function hardReset(esploader: ESPLoader, usbOtg: boolean): Promise<void> {
  try {
    await esploader.after('hard_reset', usbOtg)
  } catch {
    // best-effort
  }
}
