import { loadManifest, loadAsset, type Manifest } from './release'
import * as serial from './serial'
import { categorizeConnectError, categorizeFlashError, isUserCancellation } from './errorMessages'
import type { Transport, ESPLoader } from 'esptool-js'
import boards from '../generated/boards.json'

export type ConnectStatus = 'idle' | 'connecting' | 'connected' | 'error'
export type FlashStatus = 'idle' | 'downloading' | 'flashing' | 'done' | 'error'

export function createFlashState() {
  let board = $state('')
  let connectStatus = $state<ConnectStatus>('idle')
  let connectError = $state<string | null>(null)
  let chipInfo = $state<{ chip: string; mac: string; flashSize: string } | null>(null)
  let esploader = $state<ESPLoader | null>(null)
  let transport = $state<Transport | null>(null)
  // Use $state.raw so identity comparisons in handleDeviceDisconnect work correctly
  // (plain $state wraps objects in reactive proxies, breaking !== checks)
  let activePort = $state.raw<unknown>(null)
  let deviceDisconnected = $state(false)
  let flashStatus = $state<FlashStatus>('idle')
  let flashError = $state<string | null>(null)
  let manifest = $state<Manifest | null>(null)
  let manifestError = $state<string | null>(null)
  let downloadedBin = $state<Uint8Array | null>(null)
  let downloadProgress = $state<{ loaded: number; total: number } | null>(null)
  let flashProgress = $state<{ written: number; total: number } | null>(null)

  const boardOptions = $derived(
    boards
      .filter(b => manifest?.assets[b.id])
      .map(b => ({
        value: b.id,
        label: `${b.label}${b.asic ? ` (${b.asic})` : ''}`
      }))
  )

  async function loadManifestAction() {
    try {
      const m = await loadManifest()
      manifest = m
      manifestError = null
    } catch (e) {
      manifestError = e instanceof Error ? e.message : String(e)
    }
  }

  async function selectBoard(id: string) {
    board = id
  }

  async function connect() {
    connectError = null
    deviceDisconnected = false
    connectStatus = 'connecting'
    try {
      const port = await serial.requestPort()
      activePort = port
      const device = await serial.connectDevice(port)
      transport = device.transport
      esploader = device.esploader
      chipInfo = device.chipInfo
      connectStatus = 'connected'
    } catch (e) {
      await serial.disconnectDevice(transport)
      transport = null
      activePort = null
      const raw = e instanceof Error ? e.message : String(e)
      if (isUserCancellation(raw)) {
        connectStatus = 'idle'
        connectError = null
        return
      }
      connectStatus = 'error'
      const result = categorizeConnectError(raw, deviceDisconnected)
      if (result.kind === 'cancelled') {
        connectStatus = 'idle'
        connectError = null
      } else {
        connectError = result.message
      }
    }
  }

  async function disconnect() {
    await serial.disconnectDevice(transport)
    transport = null
    esploader = null
    chipInfo = null
    activePort = null
    connectStatus = 'idle'
  }

  async function flash() {
    if (!esploader || !manifest) return
    flashError = null
    flashProgress = null
    try {
      if (!downloadedBin) {
        flashStatus = 'downloading'
        const entry = manifest.assets[board]
        if (!entry) throw new Error(`No firmware asset for board ${board}`)
        downloadedBin = await loadAsset(entry, (loaded, total) => {
          downloadProgress = { loaded, total }
        })
      }
      flashStatus = 'flashing'
      await serial.writeFirmware(esploader, downloadedBin, (written, total) => {
        flashProgress = { written, total }
      })
      const boardEntry = boards.find(b => b.id === board)
      await serial.hardReset(esploader, boardEntry?.usbOtg ?? false)
      await serial.disconnectDevice(transport)
      transport = null
      esploader = null
      activePort = null
      chipInfo = null
      connectStatus = 'idle'
      flashStatus = 'done'
    } catch (e) {
      flashStatus = 'error'
      const raw = e instanceof Error ? e.message : String(e)
      flashError = categorizeFlashError(raw, deviceDisconnected)
    }
  }

  async function flashAnother() {
    await serial.disconnectDevice(transport)
    transport = null
    esploader = null
    chipInfo = null
    connectError = null
    connectStatus = 'idle'
    flashStatus = 'idle'
    flashError = null
  }

  function handleDeviceDisconnect(target: unknown) {
    if (!activePort || target !== activePort) return
    deviceDisconnected = true
    transport = null
    esploader = null
    chipInfo = null
    activePort = null
    connectStatus = 'idle'
    if (flashStatus === 'flashing' || flashStatus === 'downloading') {
      flashStatus = 'error'
      flashError = 'Device disconnected mid-operation'
    }
  }

  return {
    get board() { return board },
    get connectStatus() { return connectStatus },
    get connectError() { return connectError },
    get chipInfo() { return chipInfo },
    get flashStatus() { return flashStatus },
    get flashError() { return flashError },
    get manifest() { return manifest },
    get manifestError() { return manifestError },
    get downloadProgress() { return downloadProgress },
    get flashProgress() { return flashProgress },
    get deviceDisconnected() { return deviceDisconnected },
    get boardOptions() { return boardOptions },
    get transport() { return transport },
    loadManifestAction,
    selectBoard,
    connect,
    disconnect,
    flash,
    flashAnother,
    handleDeviceDisconnect,
  }
}
