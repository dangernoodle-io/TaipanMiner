import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render } from '@testing-library/svelte'
import { stats, fan, hasAsic } from '../lib/stores'

// Hoist mock stub to avoid TDZ
const { mockSs } = vi.hoisted(() => ({
  mockSs: {
    loading: false,
    loadErr: '',
    displayOn: true,
    otaSkip: false,
    mdnsOn: false,
    knotOn: false,
    savingDisplay: false,
    savingOtaSkip: false,
    savingMdns: false,
    savingKnot: false,
    displayMsg: '',
    otaMsg: '',
    mdnsMsg: '',
    knotMsg: '',
    displayKind: '' as '' | 'ok' | 'err',
    otaKind: '' as '' | 'ok' | 'err',
    mdnsKind: '' as '' | 'ok' | 'err',
    knotKind: '' as '' | 'ok' | 'err',
    loadSettings: vi.fn(),
    saveDisplay: vi.fn(),
    saveOtaSkip: vi.fn(),
    saveMdns: vi.fn(),
    saveKnot: vi.fn(),
    onDisplayChange: vi.fn(),
    onOtaChange: vi.fn(),
    onMdnsChange: vi.fn(),
    onKnotChange: vi.fn(),
  }
}))

vi.mock('../lib/settingsState.svelte', () => ({
  createSettingsState: () => mockSs,
}))

vi.mock('../lib/api', () => ({
  fetchSettings: vi.fn(),
  patchSettings: vi.fn(),
}))

import Settings from './Settings.svelte'

const baseStats = {
  session_shares: 10, session_rejected: 1, lifetime: { shares: 1000, best_diff: 250000 }, last_share_ago_s: 30,
  best_diff: 500000, uptime_s: 3600, temp_c: 40, hashrate: 485e9, hashrate_avg: 480e9,
  hashrate_1m: null, hashrate_10m: null, hashrate_1h: null, shares: null, asic_hashrate: null,
  asic_hashrate_avg: null, asic_shares: null, asic_temp_c: 72, asic_freq_configured_mhz: 400,
  asic_freq_effective_mhz: 395, asic_small_cores: 256, asic_count: 2, expected_ghs: 485,
  asic_total_ghs: 485.5, asic_hw_error_pct: 0.01, asic_total_ghs_1m: 484, asic_total_ghs_10m: 486,
  asic_total_ghs_1h: 483, asic_hw_error_pct_1m: 0.01, asic_hw_error_pct_10m: 0.01,
  asic_hw_error_pct_1h: 0.02,
  hw_error_pct_1m: null, hw_error_pct_10m: null, hw_error_pct_1h: null,
  pool_effective_hashrate: null, rejected: null
}

beforeEach(() => {
  vi.clearAllMocks()
  stats.set(null)
  fan.set(null)
  hasAsic.set(false)
  // Reset mock state to defaults
  mockSs.loading = false
  mockSs.loadErr = ''
  mockSs.displayOn = true
  mockSs.otaSkip = false
  mockSs.mdnsOn = false
  mockSs.knotOn = false
  mockSs.savingDisplay = false
  mockSs.savingOtaSkip = false
  mockSs.savingMdns = false
  mockSs.savingKnot = false
  mockSs.displayMsg = ''
  mockSs.otaMsg = ''
  mockSs.mdnsMsg = ''
  mockSs.knotMsg = ''
  mockSs.displayKind = ''
  mockSs.otaKind = ''
  mockSs.mdnsKind = ''
  mockSs.knotKind = ''
})

describe('Settings — loading state', () => {
  it('renders loading message when ss.loading=true', () => {
    mockSs.loading = true
    const { container } = render(Settings)
    expect(container.querySelector('.loading')).not.toBeNull()
    expect(container.textContent).toContain('Loading settings')
  })

  it('does not render settings sections while loading', () => {
    mockSs.loading = true
    const { container } = render(Settings)
    expect(container.querySelector('.settings')).toBeNull()
  })
})

describe('Settings — error state', () => {
  it('renders error message when ss.loadErr is set', () => {
    mockSs.loadErr = 'network error'
    const { container } = render(Settings)
    expect(container.querySelector('.error')).not.toBeNull()
    expect(container.textContent).toContain('Failed to load settings: network error')
  })

  it('does not render settings sections on error', () => {
    mockSs.loadErr = 'oops'
    const { container } = render(Settings)
    expect(container.querySelector('.settings')).toBeNull()
  })
})

describe('Settings — General section', () => {
  it('renders General heading', () => {
    const { container } = render(Settings)
    expect(container.textContent).toContain('General')
  })

  it('renders OLED display row', () => {
    const { container } = render(Settings)
    expect(container.textContent).toContain('OLED display')
  })

  it('renders OTA check on boot row', () => {
    const { container } = render(Settings)
    expect(container.textContent).toContain('OTA check on boot')
  })

  it('shows displayMsg when set', () => {
    mockSs.displayMsg = 'Saved'
    const { container } = render(Settings)
    expect(container.textContent).toContain('Saved')
  })

  it('shows otaMsg when set', () => {
    mockSs.otaMsg = 'Saved — reboot to apply'
    const { container } = render(Settings)
    expect(container.textContent).toContain('Saved — reboot to apply')
  })

  it('does not render displayMsg span when displayMsg is empty', () => {
    mockSs.displayMsg = ''
    const { container } = render(Settings)
    const statusSpans = container.querySelectorAll('.status')
    expect(statusSpans.length).toBe(0)
  })

  it('renders status span with data-kind=ok for displayKind=ok', () => {
    mockSs.displayMsg = 'Saved'
    mockSs.displayKind = 'ok'
    const { container } = render(Settings)
    const span = container.querySelector('.status[data-kind="ok"]')
    expect(span).not.toBeNull()
  })

  it('renders status span with data-kind=err for displayKind=err', () => {
    mockSs.displayMsg = 'save failed'
    mockSs.displayKind = 'err'
    const { container } = render(Settings)
    const span = container.querySelector('.status[data-kind="err"]')
    expect(span).not.toBeNull()
  })
})

describe('Settings — Network section', () => {
  it('renders Network heading', () => {
    const { container } = render(Settings)
    expect(container.textContent).toContain('Network')
  })

  it('renders Hostname row', () => {
    const { container } = render(Settings)
    expect(container.textContent).toContain('Hostname')
  })
})

describe('Settings — ASIC section (hasAsic=false)', () => {
  it('does not render ASIC heading when hasAsic=false', () => {
    hasAsic.set(false)
    const { container } = render(Settings)
    expect(container.textContent).not.toContain('ASIC')
  })

  it('does not render Fan section when hasAsic=false', () => {
    hasAsic.set(false)
    const { container } = render(Settings)
    expect(container.textContent).not.toContain('Fan')
  })
})

describe('Settings — ASIC section (hasAsic=true)', () => {
  it('renders ASIC heading', () => {
    stats.set(baseStats as any)
    hasAsic.set(true)
    const { container } = render(Settings)
    expect(container.textContent).toContain('ASIC')
  })

  it('renders Fan heading', () => {
    stats.set(baseStats as any)
    hasAsic.set(true)
    const { container } = render(Settings)
    expect(container.textContent).toContain('Fan')
  })

  it('renders per-chip rows based on asic_count', () => {
    stats.set({ ...baseStats, asic_count: 3 } as any)
    hasAsic.set(true)
    const { container } = render(Settings)
    const chipRows = container.querySelectorAll('.asic-row')
    expect(chipRows.length).toBe(3)
  })

  it('renders single chip row when asic_count=1', () => {
    stats.set({ ...baseStats, asic_count: 1 } as any)
    hasAsic.set(true)
    const { container } = render(Settings)
    const chipRows = container.querySelectorAll('.asic-row')
    expect(chipRows.length).toBe(1)
  })

  it('shows TA-194 tag when chipCount > 1', () => {
    stats.set({ ...baseStats, asic_count: 2 } as any)
    hasAsic.set(true)
    const { container } = render(Settings)
    expect(container.textContent).toContain('TA-194')
  })

  it('does not show TA-194 tag when chipCount=1', () => {
    stats.set({ ...baseStats, asic_count: 1 } as any)
    hasAsic.set(true)
    const { container } = render(Settings)
    expect(container.textContent).not.toContain('TA-194')
  })
})

describe('Settings — Fan section', () => {
  beforeEach(() => {
    stats.set(baseStats as any)
    hasAsic.set(true)
  })

  it('shows Auto mode when autofan=true', () => {
    fan.set({
      autofan: true, die_target_c: 65, vr_target_c: 80, min_pct: 35, manual_pct: 80,
      duty_pct: 75, rpm: 3200, pid_input_src: 'die', pid_input_c: 62, die_ema_c: 61, vr_ema_c: null
    })
    const { container } = render(Settings)
    expect(container.textContent).toContain('Auto (target temp)')
  })

  it('shows Manual mode when autofan=false', () => {
    fan.set({
      autofan: false, die_target_c: 65, vr_target_c: 80, min_pct: 35, manual_pct: 80,
      duty_pct: 80, rpm: 3200, pid_input_src: 'die', pid_input_c: null, die_ema_c: null, vr_ema_c: null
    })
    const { container } = render(Settings)
    expect(container.textContent).toContain('Manual duty')
  })

  it('shows die target when autofan=true', () => {
    fan.set({
      autofan: true, die_target_c: 65, vr_target_c: 80, min_pct: 35, manual_pct: 80,
      duty_pct: 75, rpm: 3200, pid_input_src: 'die', pid_input_c: null, die_ema_c: null, vr_ema_c: null
    })
    const { container } = render(Settings)
    expect(container.textContent).toContain('65°C')
  })

  it('renders live fan stats line', () => {
    fan.set({
      autofan: false, die_target_c: 65, vr_target_c: 80, min_pct: 35, manual_pct: 80,
      duty_pct: 80, rpm: 3200, pid_input_src: 'die', pid_input_c: null, die_ema_c: null, vr_ema_c: null
    })
    const { container } = render(Settings)
    expect(container.textContent).toContain('Live:')
    expect(container.textContent).toContain('3200 rpm')
  })

  it('renders — for null duty_pct', () => {
    fan.set({
      autofan: false, die_target_c: 65, vr_target_c: 80, min_pct: 35, manual_pct: 80,
      duty_pct: null, rpm: null, pid_input_src: 'die', pid_input_c: null, die_ema_c: null, vr_ema_c: null
    })
    const { container } = render(Settings)
    expect(container.textContent).toContain('—%')
  })
})

describe('Settings — calls loadSettings on mount', () => {
  it('invokes ss.loadSettings', () => {
    render(Settings)
    expect(mockSs.loadSettings).toHaveBeenCalledTimes(1)
  })
})

describe('Settings — mDNS toggle', () => {
  it('renders mDNS toggle in General section', () => {
    const { container } = render(Settings)
    expect(container.textContent).toContain('mDNS')
  })

  it('shows mdnsMsg when set', () => {
    mockSs.mdnsMsg = 'Saved'
    const { container } = render(Settings)
    expect(container.textContent).toContain('Saved')
  })

  it('renders status span with data-kind=ok for mdnsKind=ok', () => {
    mockSs.mdnsMsg = 'Saved'
    mockSs.mdnsKind = 'ok'
    const { container } = render(Settings)
    const spans = container.querySelectorAll('.status[data-kind="ok"]')
    expect(spans.length).toBeGreaterThan(0)
  })

  it('renders status span with data-kind=err for mdnsKind=err', () => {
    mockSs.mdnsMsg = 'save failed'
    mockSs.mdnsKind = 'err'
    const { container } = render(Settings)
    const spans = container.querySelectorAll('.status[data-kind="err"]')
    expect(spans.length).toBeGreaterThan(0)
  })
})

describe('Settings — Knot toggle', () => {
  it('renders Knot toggle in General section', () => {
    mockSs.mdnsOn = true
    const { container } = render(Settings)
    expect(container.textContent).toContain('Knot')
  })

  it('shows knotMsg when set', () => {
    mockSs.mdnsOn = true
    mockSs.knotMsg = 'Saved'
    const { container } = render(Settings)
    expect(container.textContent).toContain('Saved')
  })

  it('renders status span with data-kind=ok for knotKind=ok', () => {
    mockSs.mdnsOn = true
    mockSs.knotMsg = 'Saved'
    mockSs.knotKind = 'ok'
    const { container } = render(Settings)
    const spans = container.querySelectorAll('.status[data-kind="ok"]')
    expect(spans.length).toBeGreaterThan(0)
  })
})
