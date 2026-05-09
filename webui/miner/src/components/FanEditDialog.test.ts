import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen, fireEvent } from '@testing-library/svelte'
import { fan, fanEditOpen } from '../lib/stores'

vi.mock('../lib/api', () => ({
  fetchStats: vi.fn(),
  fetchInfo: vi.fn(),
  fetchPower: vi.fn(),
  fetchFan: vi.fn().mockResolvedValue({
    autofan: false,
    die_target_c: 65,
    vr_target_c: 80,
    min_pct: 35,
    manual_pct: 80,
    duty_pct: 80,
    rpm: 3200,
    pid_input_src: null,
    pid_input_c: null
  }),
  patchFan: vi.fn().mockResolvedValue({}),
  fetchSettings: vi.fn(),
  fetchPool: vi.fn(),
  fetchHealth: vi.fn(),
  ping: vi.fn()
}))

import FanEditDialog from './FanEditDialog.svelte'

const fakeFan = {
  autofan: false,
  die_target_c: 65,
  vr_target_c: 80,
  min_pct: 35,
  manual_pct: 80,
  duty_pct: 80,
  rpm: 3200,
  pid_input_src: 'die' as 'die' | 'vr',
  pid_input_c: null as number | null,
  die_ema_c: null as number | null,
  vr_ema_c: null as number | null
}

describe('FanEditDialog', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    fanEditOpen.set(false)
    fan.set(null)
  })

  it('does not render when fanEditOpen=false', () => {
    render(FanEditDialog)
    expect(document.querySelector('[role="dialog"]')).toBeNull()
  })

  it('renders dialog when fanEditOpen=true', () => {
    fan.set(fakeFan)
    fanEditOpen.set(true)
    render(FanEditDialog)
    expect(document.querySelector('[role="dialog"]')).not.toBeNull()
  })

  it('renders Fan heading', () => {
    fan.set(fakeFan)
    fanEditOpen.set(true)
    render(FanEditDialog)
    expect(screen.getByText('Fan')).toBeInTheDocument()
  })

  it('shows live duty and rpm', () => {
    fan.set(fakeFan)
    fanEditOpen.set(true)
    render(FanEditDialog)
    // live-line contains "Live: 80% · 3200 rpm"; there may be multiple 80% elements (slider val)
    const liveEl = document.querySelector('.live-line')
    expect(liveEl?.textContent).toContain('3200 rpm')
  })

  it('renders Save and Cancel buttons', () => {
    fan.set(fakeFan)
    fanEditOpen.set(true)
    render(FanEditDialog)
    expect(screen.getByRole('button', { name: 'Save' })).toBeInTheDocument()
    expect(screen.getByRole('button', { name: 'Cancel' })).toBeInTheDocument()
  })

  it('closes dialog on Cancel click when not saving', async () => {
    fan.set(fakeFan)
    fanEditOpen.set(true)
    render(FanEditDialog)
    await fireEvent.click(screen.getByRole('button', { name: 'Cancel' }))
    expect(document.querySelector('[role="dialog"]')).toBeNull()
  })

  it('has aria-modal attribute', () => {
    fan.set(fakeFan)
    fanEditOpen.set(true)
    render(FanEditDialog)
    expect(document.querySelector('[aria-modal="true"]')).not.toBeNull()
  })

  it('renders Autofan toggle', () => {
    fan.set(fakeFan)
    fanEditOpen.set(true)
    render(FanEditDialog)
    expect(screen.getByText('Autofan')).toBeInTheDocument()
  })

  it('shows Manual badge when autofan=false', () => {
    fan.set(fakeFan)
    fanEditOpen.set(true)
    render(FanEditDialog)
    expect(screen.getByText('Manual')).toBeInTheDocument()
  })

  it('shows PID source when autofan=true', () => {
    fan.set({
      ...fakeFan,
      autofan: true,
      pid_input_src: 'die',
      pid_input_c: 72.5
    })
    fanEditOpen.set(true)
    render(FanEditDialog)
    expect(screen.getByText(/PID following ASIC/)).toBeInTheDocument()
  })
})
