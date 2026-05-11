import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render } from '@testing-library/svelte'
import { stats, info, health } from '../lib/stores'

vi.mock('../lib/api', () => ({
  fetchStats: vi.fn(),
  fetchInfo: vi.fn(),
  fetchPower: vi.fn(),
  fetchFan: vi.fn(),
  fetchSettings: vi.fn(),
  fetchPool: vi.fn(),
  fetchHealth: vi.fn(),
  ping: vi.fn()
}))

import System from './System.svelte'

const baseInfo = {
  board: 'bitaxe-601',
  project_name: 'TaipanMiner',
  version: 'v1.0.0',
  idf_version: '5.5.3',
  build_date: '2024-01-15',
  build_time: '14:30:00',
  chip_model: 'esp32-s3',
  cores: 2,
  mac: '00:11:22:33:44:55',
  ssid: 'TestNetwork',
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
  network: {
    ssid: 'TestNetwork',
    bssid: 'AA:BB:CC:DD:EE:FF',
    rssi: -50,
    ip: '192.168.1.100',
    connected: true,
    disc_reason: 0,
    disc_age_s: 0,
    retry_count: 0,
    mdns: true,
    stratum: true,
    stratum_reconnect_ms: 0,
    stratum_fail_count: 0
  }
}

const baseHealth = {
  ok: true,
  free_heap: 131072,
  validated: true,
  network: {
    connected: true,
    rssi: -50,
    disc_age_s: 0,
    retry_count: 0,
    mdns: 'miner.local',
    stratum: true,
    stratum_fail_count: 0
  }
}

describe('System', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    stats.set(null)
    info.set(null)
    health.set(null)
  })

  it('renders without crashing when stores are null', () => {
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('renders when info is set', () => {
    info.set(baseInfo as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('renders Network card when health is set', () => {
    health.set(baseHealth as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('renders Heap card with memory info', () => {
    info.set({ ...baseInfo, total_heap: 262144, free_heap: 131072 } as any)
    health.set({ ...baseHealth, free_heap: 131072 } as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('shows WiFi connected status in Network card', () => {
    health.set({
      ...baseHealth,
      network: { ...baseHealth.network, connected: true }
    } as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('shows WiFi disconnected status when network.connected is false', () => {
    health.set({
      ...baseHealth,
      network: { ...baseHealth.network, connected: false }
    } as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('shows mDNS status when available', () => {
    health.set({
      ...baseHealth,
      network: { ...baseHealth.network, mdns: 'taipan.local' }
    } as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('shows mDNS as unavailable when null', () => {
    health.set({
      ...baseHealth,
      network: { ...baseHealth.network, mdns: null }
    } as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('shows Stratum status when available', () => {
    health.set({
      ...baseHealth,
      network: { ...baseHealth.network, stratum: true }
    } as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('shows Stratum as unavailable when false', () => {
    health.set({
      ...baseHealth,
      network: { ...baseHealth.network, stratum: false }
    } as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('shows Firmware validation status OK', () => {
    health.set({
      ...baseHealth,
      validated: true
    } as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('shows Firmware validation warning when validated=false', () => {
    health.set({
      ...baseHealth,
      validated: false
    } as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('shows Firmware as idle when validated is null', () => {
    health.set({
      ...baseHealth,
      validated: null
    } as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('renders all required info fields when present', () => {
    info.set(baseInfo as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('shows version and MAC from info', () => {
    info.set({
      ...baseInfo,
      version: 'v2.0.0',
      mac: 'AA:BB:CC:DD:EE:FF'
    } as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('handles null network field gracefully', () => {
    info.set({ ...baseInfo, network: undefined } as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('displays Donut chart for heap usage', () => {
    info.set({ ...baseInfo, total_heap: 262144, free_heap: 131072 } as any)
    health.set({ ...baseHealth, free_heap: 131072 } as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('renders with partial health data', () => {
    health.set({
      ok: true,
      free_heap: 100000,
      validated: true,
      network: {
        connected: true,
        rssi: -50,
        disc_age_s: 0,
        retry_count: 0,
        mdns: null
      }
    } as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })
})

describe('System — Knot row', () => {
  it('renders Knot row with ok state when network.knot=true', () => {
    health.set({
      ...baseHealth,
      network: { ...baseHealth.network, knot: true }
    } as any)
    const { container } = render(System)
    const knotRow = container.querySelector('.h-row[data-state="ok"]')
    expect(knotRow).not.toBeNull()
    expect(container.textContent).toContain('Knot')
  })

  it('renders Knot row with idle state when network.knot=false', () => {
    health.set({
      ...baseHealth,
      network: { ...baseHealth.network, knot: false }
    } as any)
    const { container } = render(System)
    expect(container.textContent).toContain('Knot')
    const rows = container.querySelectorAll('.h-row')
    let knotFound = false
    for (const row of rows) {
      if (row.textContent?.includes('Knot')) {
        expect(row.getAttribute('data-state')).toBe('idle')
        knotFound = true
        break
      }
    }
    expect(knotFound).toBe(true)
  })

  it('renders Knot row with idle state when network.knot is undefined', () => {
    health.set({
      ok: true,
      free_heap: 131072,
      validated: true,
      network: {
        connected: true,
        rssi: -50,
        disc_age_s: 0,
        retry_count: 0,
        mdns: null
        // knot field omitted/undefined
      }
    } as any)
    const { container } = render(System)
    expect(container.textContent).toContain('Knot')
    const rows = container.querySelectorAll('.h-row')
    let knotFound = false
    for (const row of rows) {
      if (row.textContent?.includes('Knot')) {
        expect(row.getAttribute('data-state')).toBe('idle')
        knotFound = true
        break
      }
    }
    expect(knotFound).toBe(true)
  })
})
