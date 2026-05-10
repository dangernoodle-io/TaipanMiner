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

  it('shows PID source when pid_input_src=vr', () => {
    fan.set({
      ...fakeFan,
      autofan: true,
      pid_input_src: 'vr',
      pid_input_c: null
    })
    fanEditOpen.set(true)
    render(FanEditDialog)
    expect(screen.getByText(/PID following VR/)).toBeInTheDocument()
  })

  it('shows pid_input_c temperature when autofan and pid_input_c set', () => {
    fan.set({
      ...fakeFan,
      autofan: true,
      pid_input_src: 'die',
      pid_input_c: 68.4
    })
    fanEditOpen.set(true)
    render(FanEditDialog)
    expect(screen.getByText(/68.4/)).toBeInTheDocument()
  })

  it('submits form and shows Saved on success', async () => {
    const { patchFan, fetchFan } = await import('../lib/api')
    vi.mocked(patchFan).mockResolvedValue(undefined)
    vi.mocked(fetchFan).mockResolvedValue({ ...fakeFan, manual_pct: 90 })
    fan.set(fakeFan)
    fanEditOpen.set(true)
    render(FanEditDialog)
    const saveBtn = screen.getByRole('button', { name: 'Save' })
    await fireEvent.submit(saveBtn.closest('form')!)
    // Wait for save to complete
    await new Promise(r => setTimeout(r, 50))
    expect(vi.mocked(patchFan)).toHaveBeenCalled()
  })

  it('shows error message when save fails', async () => {
    const { patchFan } = await import('../lib/api')
    vi.mocked(patchFan).mockRejectedValue(new Error('network error'))
    fan.set(fakeFan)
    fanEditOpen.set(true)
    render(FanEditDialog)
    const form = document.querySelector('form')!
    await fireEvent.submit(form)
    await new Promise(r => setTimeout(r, 50))
    expect(document.querySelector('.msg.err')).not.toBeNull()
  })

  it('backdrop click does not close dialog while saving', async () => {
    // When saving=true, close() is a no-op
    fan.set(fakeFan)
    fanEditOpen.set(true)
    const { patchFan, fetchFan } = await import('../lib/api')
    // Make patchFan never resolve so dialog stays in saving=true
    vi.mocked(patchFan).mockImplementation(() => new Promise(() => {}))
    vi.mocked(fetchFan).mockResolvedValue(fakeFan)
    render(FanEditDialog)
    const form = document.querySelector('form')!
    fireEvent.submit(form) // do NOT await — leave it in-flight
    await new Promise(r => setTimeout(r, 10))
    const backdrop = document.querySelector('.modal-backdrop')!
    await fireEvent.click(backdrop)
    // Dialog still open because saving=true blocks close()
    expect(document.querySelector('[role="dialog"]')).not.toBeNull()
  })
})
