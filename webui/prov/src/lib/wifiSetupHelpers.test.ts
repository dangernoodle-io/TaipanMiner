import { describe, it, expect } from 'vitest'
import { validateForm, resolvedSsid } from './wifiSetupHelpers'

const base = {
  selectedSsid: 'TestNet',
  manualSsid: '',
  wallet: '1BTCxxx',
  worker: 'miner-1',
  poolHost: 'pool.example.com',
  poolPort: '3333',
}

describe('validateForm — happy path', () => {
  it('returns empty errors for valid input', () => {
    expect(validateForm(base)).toEqual({})
  })
})

describe('validateForm — ssid validation', () => {
  it('errors when selectedSsid is empty', () => {
    const errors = validateForm({ ...base, selectedSsid: '' })
    expect(errors.ssid).toBe('Network is required')
  })

  it('no ssid error when selectedSsid is set', () => {
    const errors = validateForm(base)
    expect(errors.ssid).toBeUndefined()
  })

  it('errors when __manual__ selected but manualSsid is empty', () => {
    const errors = validateForm({ ...base, selectedSsid: '__manual__', manualSsid: '' })
    expect(errors.ssid).toBe('Network is required')
  })

  it('no ssid error when __manual__ with non-empty manualSsid', () => {
    const errors = validateForm({ ...base, selectedSsid: '__manual__', manualSsid: 'MyNet' })
    expect(errors.ssid).toBeUndefined()
  })
})

describe('validateForm — wallet validation', () => {
  it('errors when wallet is empty', () => {
    const errors = validateForm({ ...base, wallet: '' })
    expect(errors.wallet).toBe('Required')
  })

  it('errors when wallet is whitespace only', () => {
    const errors = validateForm({ ...base, wallet: '   ' })
    expect(errors.wallet).toBe('Required')
  })

  it('no wallet error when wallet has content', () => {
    const errors = validateForm(base)
    expect(errors.wallet).toBeUndefined()
  })
})

describe('validateForm — worker validation', () => {
  it('errors when worker is empty', () => {
    const errors = validateForm({ ...base, worker: '' })
    expect(errors.worker).toBe('Required')
  })

  it('errors when worker is whitespace only', () => {
    const errors = validateForm({ ...base, worker: '   ' })
    expect(errors.worker).toBe('Required')
  })
})

describe('validateForm — poolHost validation', () => {
  it('errors when poolHost is empty', () => {
    const errors = validateForm({ ...base, poolHost: '' })
    expect(errors.poolHost).toBe('Required')
  })

  it('errors when poolHost is whitespace only', () => {
    const errors = validateForm({ ...base, poolHost: '   ' })
    expect(errors.poolHost).toBe('Required')
  })
})

describe('validateForm — poolPort validation', () => {
  it('errors when poolPort is empty', () => {
    const errors = validateForm({ ...base, poolPort: '' })
    expect(errors.poolPort).toBe('Valid port (1–65535) required')
  })

  it('errors when poolPort is 0', () => {
    const errors = validateForm({ ...base, poolPort: '0' })
    expect(errors.poolPort).toBe('Valid port (1–65535) required')
  })

  it('errors when poolPort is 65536', () => {
    const errors = validateForm({ ...base, poolPort: '65536' })
    expect(errors.poolPort).toBe('Valid port (1–65535) required')
  })

  it('errors when poolPort is non-numeric', () => {
    const errors = validateForm({ ...base, poolPort: 'abc' })
    expect(errors.poolPort).toBe('Valid port (1–65535) required')
  })

  it('no port error for port 1', () => {
    const errors = validateForm({ ...base, poolPort: '1' })
    expect(errors.poolPort).toBeUndefined()
  })

  it('no port error for port 65535', () => {
    const errors = validateForm({ ...base, poolPort: '65535' })
    expect(errors.poolPort).toBeUndefined()
  })

  it('no port error for 3333', () => {
    const errors = validateForm({ ...base, poolPort: '3333' })
    expect(errors.poolPort).toBeUndefined()
  })
})

describe('resolvedSsid', () => {
  it('returns manualSsid when selectedSsid is __manual__', () => {
    expect(resolvedSsid('__manual__', 'MyHiddenNet')).toBe('MyHiddenNet')
  })

  it('returns selectedSsid when not __manual__', () => {
    expect(resolvedSsid('HomeNet', 'ignored')).toBe('HomeNet')
  })

  it('returns empty string when selectedSsid is empty', () => {
    expect(resolvedSsid('', '')).toBe('')
  })
})
