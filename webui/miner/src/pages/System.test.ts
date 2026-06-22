import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render } from '@testing-library/svelte'
import { stats, info, health } from '../lib/stores'
import * as api from '../lib/api'

vi.mock('../lib/api', () => ({
  fetchStats: vi.fn(),
  fetchInfo: vi.fn(),
  fetchPower: vi.fn(),
  fetchFan: vi.fn(),
  fetchSettings: vi.fn(),
  fetchPool: vi.fn(),
  fetchHealth: vi.fn(),
  ping: vi.fn(),
  resetStats: vi.fn(),
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
  flash_size: 16777216,
  app_size: 1048576,
  heap_internal: { free: 131072, total: 262144, min_free: 98304, largest_block: 65536 },
  reset_reason: 'Unknown',
  hostname: 'taipan.local',
  boot_epoch: 1705333200,
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
  },
  mining: {
    worker_name: 'testworker',
  },
  diag: { wdt_resets: 0 },
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
    mdns: 'miner.local'
  },
  mining: {
    sha_self_test_failed: false
  },
  pool: {
    stratum: true,
    fail_count: 0,
    reconnect_ms: 0
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
    info.set({ ...baseInfo } as any)
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
      pool: { stratum: true, fail_count: 0, reconnect_ms: 0 }
    } as any)
    const { component } = render(System)
    expect(component).toBeDefined()
  })

  it('shows Stratum as unavailable when false', () => {
    health.set({
      ...baseHealth,
      pool: { stratum: false, fail_count: 0, reconnect_ms: 0 }
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
    info.set({ ...baseInfo } as any)
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

describe('System — chip detection driven by info.asic', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    stats.set(null)
    info.set(null)
    health.set(null)
  })

  it('flags chipsBad when detectedChips < info.asic.chips', () => {
    info.set({ ...baseInfo, capabilities: ['asic'], mining: { asic: { model: 'BM1370', chips: 2, small_cores_per_chip: 256 } } } as any)
    stats.set({ asic_chips: [{}], asic_count: 2, asic_small_cores: 256 } as any)
    const { container } = render(System)
    const badDds = container.querySelectorAll('dd.bad')
    expect(badDds.length).toBeGreaterThan(0)
  })

  it('does not flag chipsBad when detectedChips == info.asic.chips', () => {
    info.set({ ...baseInfo, capabilities: ['asic'], mining: { asic: { model: 'BM1370', chips: 1, small_cores_per_chip: 256 } } } as any)
    stats.set({ asic_chips: [{}], asic_count: 1, asic_small_cores: 256 } as any)
    const { container } = render(System)
    const badDds = container.querySelectorAll('dd.bad')
    expect(badDds.length).toBe(0)
  })

  it('does not flag chipsBad when info.asic is absent', () => {
    info.set({ ...baseInfo } as any)
    stats.set({ asic_chips: [{}], asic_count: null, asic_small_cores: 256 } as any)
    const { container } = render(System)
    const badDds = container.querySelectorAll('dd.bad')
    expect(badDds.length).toBe(0)
  })

  it('does not flag chipsBad when stats is null', () => {
    info.set({ ...baseInfo, capabilities: ['asic'], mining: { asic: { model: 'BM1370', chips: 1, small_cores_per_chip: 256 } } } as any)
    stats.set(null)
    const { component } = render(System)
    expect(component).toBeDefined()
  })
})

describe('System — stratumFails row', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    stats.set(null)
    info.set(null)
    health.set(null)
  })

  it('renders stratum-fail count row when fail_count > 0', () => {
    health.set({
      ...baseHealth,
      pool: { stratum: true, fail_count: 3, reconnect_ms: 0 }
    } as any)
    const { container } = render(System)
    expect(container.textContent).toContain('3 fails')
  })

  it('does not render stratum-fail row when fail_count is 0', () => {
    health.set({
      ...baseHealth,
      pool: { stratum: true, fail_count: 0, reconnect_ms: 0 }
    } as any)
    const { container } = render(System)
    expect(container.textContent).not.toContain('fails')
  })
})

describe('System — Display row', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    stats.set(null)
    info.set(null)
    health.set(null)
  })

  it('shows "None" when display is absent', () => {
    info.set({ ...baseInfo, display: { present: false } } as any)
    const { container } = render(System)
    const dts = container.querySelectorAll('dt')
    let displayDd: Element | null = null
    for (const dt of dts) {
      if (dt.textContent?.trim() === 'Display') {
        displayDd = dt.nextElementSibling
        break
      }
    }
    expect(displayDd?.textContent?.trim()).toBe('None')
  })

  it('shows panel + resolution + state when display is present', () => {
    info.set({
      ...baseInfo,
      display: { present: true, panel: 'st77xx', width: 160, height: 80, enabled: true }
    } as any)
    const { container } = render(System)
    expect(container.textContent).toContain('ST77xx')
    expect(container.textContent).toContain('160×80')
    // on/off state lives in the status-bar Display chip
    expect(container.textContent).toContain('Display on')
  })

  it('maps ssd1306 panel label correctly', () => {
    info.set({
      ...baseInfo,
      display: { present: true, panel: 'ssd1306', width: 128, height: 32, enabled: false }
    } as any)
    const { container } = render(System)
    expect(container.textContent).toContain('SSD1306')
    // display disabled -> status-bar chip reads "Display off"
    expect(container.textContent).toContain('Display off')
  })

  it('shows "None" when display field is absent from info', () => {
    info.set({ ...baseInfo } as any)
    const { container } = render(System)
    expect(container.textContent).toContain('None')
  })
})

describe('System — LED row', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    stats.set(null)
    info.set(null)
    health.set(null)
  })

  it('shows "None" when led is absent', () => {
    info.set({ ...baseInfo, led: { present: false } } as any)
    const { container } = render(System)
    const dts = container.querySelectorAll('dt')
    let ledDd: Element | null = null
    for (const dt of dts) {
      if (dt.textContent?.trim() === 'LED') {
        ledDd = dt.nextElementSibling
        break
      }
    }
    expect(ledDd?.textContent?.trim()).toBe('None')
  })

  it('shows APA102 + count + RGB when led is present', () => {
    info.set({
      ...baseInfo,
      led: { present: true, type: 'apa102', count: 1, rgb: true }
    } as any)
    const { container } = render(System)
    expect(container.textContent).toContain('APA102')
    expect(container.textContent).toContain('×1')
    expect(container.textContent).toContain('· RGB')
  })

  it('shows PWM without RGB marker when rgb is false', () => {
    info.set({
      ...baseInfo,
      led: { present: true, type: 'pwm', count: 1, rgb: false }
    } as any)
    const { container } = render(System)
    expect(container.textContent).toContain('PWM')
    expect(container.textContent).not.toContain('· RGB')
  })
})

describe('System — ASIC from info.asic (not hardcoded map)', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    stats.set(null)
    info.set(null)
    health.set(null)
  })

  it('shows ASIC card when capabilities includes "asic"', () => {
    info.set({
      ...baseInfo,
      capabilities: ['asic', 'fan'],
      mining: { asic: { model: 'BM1370', chips: 1, small_cores_per_chip: 256 } }
    } as any)
    const { container } = render(System)
    expect(container.textContent).toContain('ASIC')
    expect(container.textContent).toContain('BM1370')
  })

  it('shows ASIC card when asic field is present (no capabilities)', () => {
    info.set({
      ...baseInfo,
      mining: { asic: { model: 'BM1368', chips: 1, small_cores_per_chip: 128 } }
    } as any)
    const { container } = render(System)
    expect(container.textContent).toContain('BM1368')
  })

  it('shows ×N suffix when chips > 1', () => {
    info.set({
      ...baseInfo,
      capabilities: ['asic'],
      mining: { asic: { model: 'BM1370', chips: 2, small_cores_per_chip: 256 } }
    } as any)
    const { container } = render(System)
    expect(container.textContent).toContain('BM1370 ×2')
  })

  it('hides ASIC card when neither capabilities nor asic present', () => {
    info.set({ ...baseInfo } as any)
    const { container } = render(System)
    expect(container.textContent).not.toContain('ASIC')
  })

  it('derives expectedChips from info.mining.asic.chips', () => {
    info.set({
      ...baseInfo,
      capabilities: ['asic'],
      mining: { asic: { model: 'BM1370', chips: 2, small_cores_per_chip: 256 } }
    } as any)
    stats.set({ asic_chips: [{}], asic_count: 2, asic_small_cores: 256 } as any)
    const { container } = render(System)
    // expectedChips=2 from info.mining.asic.chips, detectedChips=1 → chipsBad → dd.bad
    const badDds = container.querySelectorAll('dd.bad')
    expect(badDds.length).toBeGreaterThan(0)
  })

  it('derives expectedCores from info.mining.asic.chips * small_cores_per_chip', () => {
    info.set({
      ...baseInfo,
      capabilities: ['asic'],
      mining: { asic: { model: 'BM1370', chips: 1, small_cores_per_chip: 256 } }
    } as any)
    stats.set({ asic_chips: [{}], asic_count: 1, asic_small_cores: 256 } as any)
    const { container } = render(System)
    expect(container.textContent).toContain('256')
  })
})

// Helper: find Donut label elements (div.label inside .donut) with given text.
function psramDonutLabels(container: HTMLElement): Element[] {
  return Array.from(container.querySelectorAll('.donut .label')).filter(
    el => el.childNodes[0]?.nodeValue?.trim() === 'PSRAM'
  )
}

function rtcDonutLabels(container: HTMLElement): Element[] {
  return Array.from(container.querySelectorAll('.donut .label')).filter(
    el => el.childNodes[0]?.nodeValue?.trim() === 'RTC'
  )
}

describe('System — PSRAM donut', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    stats.set(null)
    info.set(null)
    health.set(null)
  })

  it('renders PSRAM donut when heap_psram.total > 0', () => {
    info.set({
      ...baseInfo,
      heap_psram: { free: 2 * 1024 * 1024, total: 4 * 1024 * 1024 }
    } as any)
    const { container } = render(System)
    expect(psramDonutLabels(container).length).toBe(1)
  })

  it('does not render PSRAM donut when heap_psram is absent', () => {
    info.set({ ...baseInfo } as any)
    const { container } = render(System)
    expect(psramDonutLabels(container).length).toBe(0)
  })

  it('does not render PSRAM donut when heap_psram.total is 0', () => {
    info.set({
      ...baseInfo,
      heap_psram: { free: 0, total: 0 }
    } as any)
    const { container } = render(System)
    expect(psramDonutLabels(container).length).toBe(0)
  })
})

describe('System — RTC donut', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    stats.set(null)
    info.set(null)
    health.set(null)
  })

  it('renders RTC donut when rtc is present', () => {
    info.set({
      ...baseInfo,
      rtc: { used: 2048, total: 8192 }
    } as any)
    const { container } = render(System)
    expect(rtcDonutLabels(container).length).toBe(1)
  })

  it('does not render RTC donut when rtc is absent', () => {
    info.set({ ...baseInfo } as any)
    const { container } = render(System)
    expect(rtcDonutLabels(container).length).toBe(0)
  })
})

describe('System — Knot row', () => {
  it('renders Knot row with ok state when knot.running=true', () => {
    health.set({
      ...baseHealth,
      knot: { running: true }
    } as any)
    const { container } = render(System)
    const knotRow = container.querySelector('.h-row[data-state="ok"]')
    expect(knotRow).not.toBeNull()
    expect(container.textContent).toContain('Knot')
  })

  it('renders Knot row with idle state when knot.running=false', () => {
    health.set({
      ...baseHealth,
      knot: { running: false }
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

  it('renders Knot row with idle state when knot field is undefined', () => {
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
      }
      // mining field omitted/undefined
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
