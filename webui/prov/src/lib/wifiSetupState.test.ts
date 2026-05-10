import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'

vi.mock('./api', async () => {
  const actual = await vi.importActual('./api')
  return {
    ...actual,
    fetchScan: vi.fn(),
    postSave: vi.fn(),
  }
})

import * as api from './api'
import { createWifiSetupState } from './wifiSetupState.svelte'

const makeState = (cb = vi.fn()) => createWifiSetupState(() => cb)

beforeEach(() => {
  vi.clearAllMocks()
  vi.useFakeTimers()
})

afterEach(() => {
  vi.useRealTimers()
})

describe('createWifiSetupState — initial state', () => {
  it('starts with empty networks', () => {
    const ws = makeState()
    expect(ws.networks).toEqual([])
  })

  it('starts with scanning false', () => {
    const ws = makeState()
    expect(ws.scanning).toBe(false)
  })

  it('starts with scanError null', () => {
    const ws = makeState()
    expect(ws.scanError).toBeNull()
  })

  it('starts with empty selectedSsid', () => {
    const ws = makeState()
    expect(ws.selectedSsid).toBe('')
  })

  it('starts with empty pass', () => {
    const ws = makeState()
    expect(ws.pass).toBe('')
  })

  it('starts with showPass false', () => {
    const ws = makeState()
    expect(ws.showPass).toBe(false)
  })

  it('starts with submitting false', () => {
    const ws = makeState()
    expect(ws.submitting).toBe(false)
  })

  it('starts with submitError null', () => {
    const ws = makeState()
    expect(ws.submitError).toBeNull()
  })

  it('starts with empty errors', () => {
    const ws = makeState()
    expect(ws.errors).toEqual({})
  })
})

describe('scan — happy path', () => {
  it('sets scanning true during fetch, false after', async () => {
    vi.mocked(api.fetchScan).mockResolvedValue([
      { ssid: 'HomeNet', rssi: -50, secure: true },
    ])
    const ws = makeState()
    const p = ws.scan()
    expect(ws.scanning).toBe(true)
    await vi.runAllTimersAsync()
    await p
    expect(ws.scanning).toBe(false)
  })

  it('populates networks on success', async () => {
    vi.mocked(api.fetchScan).mockResolvedValue([
      { ssid: 'NetA', rssi: -55, secure: true },
      { ssid: 'NetB', rssi: -70, secure: false },
    ])
    const ws = makeState()
    await ws.scan()
    expect(ws.networks).toHaveLength(2)
    expect(ws.networks[0].ssid).toBe('NetA')
  })

  it('auto-selects first network', async () => {
    vi.mocked(api.fetchScan).mockResolvedValue([
      { ssid: 'First', rssi: -40, secure: true },
      { ssid: 'Second', rssi: -60, secure: false },
    ])
    const ws = makeState()
    await ws.scan()
    expect(ws.selectedSsid).toBe('First')
  })

  it('clears previous networks and selection before fetch', async () => {
    vi.mocked(api.fetchScan).mockResolvedValue([])
    const ws = makeState()
    ws.selectedSsid = 'OldNet'
    ws.manualSsid = 'manual'
    const p = ws.scan()
    expect(ws.networks).toEqual([])
    expect(ws.selectedSsid).toBe('')
    expect(ws.manualSsid).toBe('')
    await p
  })

  it('clears scanError before fetch', async () => {
    vi.mocked(api.fetchScan).mockResolvedValue([])
    const ws = makeState()
    // first set an error
    vi.mocked(api.fetchScan).mockRejectedValueOnce(new Error('old error'))
    await ws.scan()
    expect(ws.scanError).toContain('old error')
    // now scan again successfully
    vi.mocked(api.fetchScan).mockResolvedValue([])
    const p = ws.scan()
    expect(ws.scanError).toBeNull()
    await p
  })

  it('leaves selectedSsid empty when no networks returned', async () => {
    vi.mocked(api.fetchScan).mockResolvedValue([])
    const ws = makeState()
    await ws.scan()
    expect(ws.selectedSsid).toBe('')
  })
})

describe('scan — error path', () => {
  it('sets scanError on fetch failure', async () => {
    vi.mocked(api.fetchScan).mockRejectedValue(new Error('network unreachable'))
    const ws = makeState()
    await ws.scan()
    expect(ws.scanError).toBe('Scan failed: network unreachable')
  })

  it('handles non-Error rejection', async () => {
    vi.mocked(api.fetchScan).mockRejectedValue('oops')
    const ws = makeState()
    await ws.scan()
    expect(ws.scanError).toBe('Scan failed: Unknown error')
  })

  it('sets scanning false after error', async () => {
    vi.mocked(api.fetchScan).mockRejectedValue(new Error('fail'))
    const ws = makeState()
    await ws.scan()
    expect(ws.scanning).toBe(false)
  })
})

describe('setter round-trips', () => {
  it('selectedSsid setter', () => {
    const ws = makeState()
    ws.selectedSsid = 'TestNet'
    expect(ws.selectedSsid).toBe('TestNet')
  })

  it('manualSsid setter', () => {
    const ws = makeState()
    ws.manualSsid = 'HiddenNet'
    expect(ws.manualSsid).toBe('HiddenNet')
  })

  it('pass setter', () => {
    const ws = makeState()
    ws.pass = 'hunter2'
    expect(ws.pass).toBe('hunter2')
  })

  it('showPass setter', () => {
    const ws = makeState()
    ws.showPass = true
    expect(ws.showPass).toBe(true)
    ws.showPass = false
    expect(ws.showPass).toBe(false)
  })

  it('wallet setter', () => {
    const ws = makeState()
    ws.wallet = '1BTCabc'
    expect(ws.wallet).toBe('1BTCabc')
  })

  it('poolHost setter', () => {
    const ws = makeState()
    ws.poolHost = 'pool.example.com'
    expect(ws.poolHost).toBe('pool.example.com')
  })

  it('poolPort setter', () => {
    const ws = makeState()
    ws.poolPort = '3333'
    expect(ws.poolPort).toBe('3333')
  })

  it('poolPass setter', () => {
    const ws = makeState()
    ws.poolPass = 'x'
    expect(ws.poolPass).toBe('x')
  })
})

describe('hostname / worker sync', () => {
  it('worker tracks hostname when not manually edited', () => {
    const ws = makeState()
    ws.hostname = 'my-miner'
    expect(ws.worker).toBe('my-miner')
  })

  it('worker does not track hostname after manual edit', () => {
    const ws = makeState()
    ws.worker = 'custom-worker'
    ws.hostname = 'new-hostname'
    expect(ws.worker).toBe('custom-worker')
  })

  it('worker setter marks as edited', () => {
    const ws = makeState()
    ws.worker = 'edited'
    ws.hostname = 'ignored'
    expect(ws.worker).toBe('edited')
  })
})

describe('validate', () => {
  it('returns true for valid form', () => {
    const ws = makeState()
    ws.selectedSsid = 'HomeNet'
    ws.wallet = '1BTCxxx'
    ws.worker = 'miner-1'
    ws.poolHost = 'pool.example.com'
    ws.poolPort = '3333'
    expect(ws.validate()).toBe(true)
    expect(ws.errors).toEqual({})
  })

  it('returns false and sets errors for invalid form', () => {
    const ws = makeState()
    expect(ws.validate()).toBe(false)
    expect(ws.errors.ssid).toBeDefined()
    expect(ws.errors.wallet).toBeDefined()
    expect(ws.errors.worker).toBeDefined()
    expect(ws.errors.poolHost).toBeDefined()
    expect(ws.errors.poolPort).toBeDefined()
  })
})

describe('handleSubmit — validation guard', () => {
  it('does not call postSave when form invalid', async () => {
    const ws = makeState()
    await ws.handleSubmit()
    expect(api.postSave).not.toHaveBeenCalled()
  })
})

describe('handleSubmit — happy path (response before timeout)', () => {
  it('calls postSave with correct payload', async () => {
    vi.mocked(api.postSave).mockResolvedValue(undefined)
    const cb = vi.fn()
    const ws = makeState(cb)
    ws.selectedSsid = 'HomeNet'
    ws.pass = 's3cr3t'
    ws.hostname = 'taipan'
    ws.wallet = '1BTCabc'
    ws.worker = 'miner-1'
    ws.poolHost = 'pool.example.com'
    ws.poolPort = '3333'
    ws.poolPass = ''
    const p = ws.handleSubmit()
    await vi.runAllTimersAsync()
    await p
    expect(api.postSave).toHaveBeenCalledWith({
      ssid: 'HomeNet',
      pass: 's3cr3t',
      hostname: 'taipan',
      wallet: '1BTCabc',
      worker: 'miner-1',
      pool_host: 'pool.example.com',
      pool_port: '3333',
      pool_pass: '',
    })
  })

  it('calls onSaved on success', async () => {
    vi.mocked(api.postSave).mockResolvedValue(undefined)
    const cb = vi.fn()
    const ws = makeState(cb)
    ws.selectedSsid = 'HomeNet'
    ws.wallet = '1BTCabc'
    ws.worker = 'miner-1'
    ws.poolHost = 'pool.example.com'
    ws.poolPort = '3333'
    const p = ws.handleSubmit()
    await vi.runAllTimersAsync()
    await p
    expect(cb).toHaveBeenCalledOnce()
  })

  it('sets submitting true during request, false on error', async () => {
    vi.mocked(api.postSave).mockRejectedValue(new Error('save failed: 500'))
    const ws = makeState()
    ws.selectedSsid = 'HomeNet'
    ws.wallet = '1BTCabc'
    ws.worker = 'miner-1'
    ws.poolHost = 'pool.example.com'
    ws.poolPort = '3333'
    const p = ws.handleSubmit()
    expect(ws.submitting).toBe(true)
    await vi.runAllTimersAsync()
    await p
    expect(ws.submitting).toBe(false)
  })
})

describe('handleSubmit — timeout path', () => {
  it('calls onSaved after 1500ms timeout (AP teardown race)', async () => {
    // postSave never resolves (simulates AP teardown)
    vi.mocked(api.postSave).mockReturnValue(new Promise(() => {}))
    const cb = vi.fn()
    const ws = makeState(cb)
    ws.selectedSsid = 'HomeNet'
    ws.wallet = '1BTCabc'
    ws.worker = 'miner-1'
    ws.poolHost = 'pool.example.com'
    ws.poolPort = '3333'
    const p = ws.handleSubmit()
    await vi.advanceTimersByTimeAsync(1500)
    await p
    expect(cb).toHaveBeenCalledOnce()
  })
})

describe('handleSubmit — error path', () => {
  it('sets submitError on postSave rejection', async () => {
    vi.mocked(api.postSave).mockRejectedValue(new Error('save failed: 500'))
    const ws = makeState()
    ws.selectedSsid = 'HomeNet'
    ws.wallet = '1BTCabc'
    ws.worker = 'miner-1'
    ws.poolHost = 'pool.example.com'
    ws.poolPort = '3333'
    const p = ws.handleSubmit()
    await vi.runAllTimersAsync()
    await p
    expect(ws.submitError).toBe('Save failed: save failed: 500')
  })

  it('handles non-Error rejection', async () => {
    vi.mocked(api.postSave).mockRejectedValue('boom')
    const ws = makeState()
    ws.selectedSsid = 'HomeNet'
    ws.wallet = '1BTCabc'
    ws.worker = 'miner-1'
    ws.poolHost = 'pool.example.com'
    ws.poolPort = '3333'
    const p = ws.handleSubmit()
    await vi.runAllTimersAsync()
    await p
    expect(ws.submitError).toBe('Save failed: Unknown error')
  })

  it('resolves __manual__ ssid correctly', async () => {
    vi.mocked(api.postSave).mockResolvedValue(undefined)
    const cb = vi.fn()
    const ws = makeState(cb)
    ws.selectedSsid = '__manual__'
    ws.manualSsid = 'HiddenNet'
    ws.wallet = '1BTCabc'
    ws.worker = 'miner-1'
    ws.poolHost = 'pool.example.com'
    ws.poolPort = '3333'
    const p = ws.handleSubmit()
    await vi.runAllTimersAsync()
    await p
    expect(api.postSave).toHaveBeenCalledWith(expect.objectContaining({ ssid: 'HiddenNet' }))
  })
})
