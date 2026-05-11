import type { Settings } from './api'

/** The subset of Settings fields that the Settings page edits live. */
export type SettingsForm = {
  display_en: boolean
  ota_skip_check: boolean
  mdns_en: boolean
  knot_en: boolean
}

/** Build a SettingsForm from a raw Settings response. */
export function formFromSettings(s: Settings): SettingsForm {
  return {
    display_en: !!s.display_en,
    ota_skip_check: !!s.ota_skip_check,
    mdns_en: !!s.mdns_en,
    knot_en: !!s.knot_en,
  }
}
