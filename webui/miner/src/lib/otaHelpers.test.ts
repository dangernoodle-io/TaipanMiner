import { describe, it, expect } from 'vitest'
import { firmwareName, minerBusy } from './otaHelpers'
import type { Info } from './api'

const baseInfo: Info = {
  board: 'bitaxe-601',
  project_name: 'TaipanMiner',
  version: 'v1.2.3',
  idf_version: '5.5.3',
  build_date: '2024-01-15',
  build_time: '14:30:00',
  chip_model: 'esp32-s3',
  cores: 2,
  mac: '00:11:22:33:44:55',
  ssid: 'TestNet',
  flash_size: 16777216,
  app_size: 1048576,
  total_heap: 262144,
  free_heap: 131072,
  reset_reason: 'Unknown',
  wdt_resets: 0,
  boot_time: 1705333200,
  worker_name: 'testworker',
  hostname: 'taipan.local',
  validated: true,
}

describe('firmwareName', () => {
  it('returns default name when info is null', () => {
    expect(firmwareName(null)).toBe('firmware.bin')
  })

  it('returns board-specific name when board is set', () => {
    expect(firmwareName(baseInfo)).toBe('taipanminer-bitaxe-601.bin')
  })

  it('returns board-specific name for different board', () => {
    expect(firmwareName({ ...baseInfo, board: 'bitaxe-403' })).toBe('taipanminer-bitaxe-403.bin')
  })

  it('returns board-specific name for tdongle-s3', () => {
    expect(firmwareName({ ...baseInfo, board: 'tdongle-s3' })).toBe('taipanminer-tdongle-s3.bin')
  })
})

describe('minerBusy', () => {
  const idle = { active: false }
  const installIdle = { kind: '' }
  const installOk = { kind: 'ok' }
  const installErr = { kind: 'err' }
  const uploadIdle = { kind: '' }
  const uploadOk = { kind: 'ok' }
  const uploadErr = { kind: 'err' }
  const rebooting = { active: true }

  it('returns false when all idle', () => {
    expect(minerBusy(idle, installIdle, uploadIdle)).toBe(false)
  })

  it('returns true when rebooting.active is true', () => {
    expect(minerBusy(rebooting, installIdle, uploadIdle)).toBe(true)
  })

  it('returns true when install.kind is ok', () => {
    expect(minerBusy(idle, installOk, uploadIdle)).toBe(true)
  })

  it('returns true when upload.kind is ok', () => {
    expect(minerBusy(idle, installIdle, uploadOk)).toBe(true)
  })

  it('returns false when install.kind is err (not busy)', () => {
    expect(minerBusy(idle, installErr, uploadIdle)).toBe(false)
  })

  it('returns false when upload.kind is err (not busy)', () => {
    expect(minerBusy(idle, installIdle, uploadErr)).toBe(false)
  })

  it('returns true when both rebooting and install ok', () => {
    expect(minerBusy(rebooting, installOk, uploadIdle)).toBe(true)
  })

  it('returns true when all three are active', () => {
    expect(minerBusy(rebooting, installOk, uploadOk)).toBe(true)
  })

  it('returns false when install is in progress (kind empty)', () => {
    expect(minerBusy(idle, { kind: '' }, uploadIdle)).toBe(false)
  })
})
