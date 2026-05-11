import { describe, it, expect } from 'vitest'
import { formFromSettings } from './settingsHelpers'
import type { Settings } from './api'

describe('formFromSettings', () => {
  it('maps display_en true to form.display_en true', () => {
    const s: Settings = { hostname: 'taipan', display_en: true, ota_skip_check: false }
    expect(formFromSettings(s).display_en).toBe(true)
  })

  it('maps display_en false to form.display_en false', () => {
    const s: Settings = { hostname: 'taipan', display_en: false, ota_skip_check: false }
    expect(formFromSettings(s).display_en).toBe(false)
  })

  it('maps undefined display_en to false', () => {
    const s: Settings = { hostname: 'taipan' }
    expect(formFromSettings(s).display_en).toBe(false)
  })

  it('maps ota_skip_check true to form.ota_skip_check true', () => {
    const s: Settings = { hostname: 'taipan', display_en: false, ota_skip_check: true }
    expect(formFromSettings(s).ota_skip_check).toBe(true)
  })

  it('maps ota_skip_check false to form.ota_skip_check false', () => {
    const s: Settings = { hostname: 'taipan', display_en: true, ota_skip_check: false }
    expect(formFromSettings(s).ota_skip_check).toBe(false)
  })

  it('maps undefined ota_skip_check to false', () => {
    const s: Settings = { hostname: 'taipan' }
    expect(formFromSettings(s).ota_skip_check).toBe(false)
  })

  it('coerces truthy display_en value to true', () => {
    // api typing allows undefined; coerce via !!
    const s = { hostname: 'taipan', display_en: 1, ota_skip_check: 0 } as unknown as Settings
    const form = formFromSettings(s)
    expect(form.display_en).toBe(true)
    expect(form.ota_skip_check).toBe(false)
  })

  it('returns both fields together from a full Settings object', () => {
    const s: Settings = { hostname: 'taipan', display_en: true, ota_skip_check: true }
    const form = formFromSettings(s)
    expect(form).toEqual({ display_en: true, ota_skip_check: true, mdns_en: false, knot_en: false })
  })

  it('does not include hostname in the form', () => {
    const s: Settings = { hostname: 'taipan', display_en: true, ota_skip_check: false }
    expect(Object.keys(formFromSettings(s))).not.toContain('hostname')
  })
})
