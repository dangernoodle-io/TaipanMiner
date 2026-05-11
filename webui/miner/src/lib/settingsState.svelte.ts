import { fetchSettings, patchSettings } from './api'
import { formFromSettings } from './settingsHelpers'

export function createSettingsState() {
  let loading = $state(true)
  let loadErr = $state('')
  let saved = $state.raw<Awaited<ReturnType<typeof fetchSettings>> | null>(null)

  let displayOn = $state(false)
  let otaSkip = $state(false)
  let mdnsOn = $state(false)
  let knotOn = $state(false)
  let savingDisplay = $state(false)
  let savingOtaSkip = $state(false)
  let savingMdns = $state(false)
  let savingKnot = $state(false)
  let displayMsg = $state('')
  let otaMsg = $state('')
  let mdnsMsg = $state('')
  let knotMsg = $state('')
  let displayKind = $state<'' | 'ok' | 'err'>('')
  let otaKind = $state<'' | 'ok' | 'err'>('')
  let mdnsKind = $state<'' | 'ok' | 'err'>('')
  let knotKind = $state<'' | 'ok' | 'err'>('')

  async function loadSettings() {
    loading = true
    loadErr = ''
    try {
      const s = await fetchSettings()
      saved = s
      const form = formFromSettings(s)
      displayOn = form.display_en
      otaSkip = form.ota_skip_check
      mdnsOn = form.mdns_en
      knotOn = form.knot_en
    } catch (e) {
      loadErr = (e as Error).message
    } finally {
      loading = false
    }
  }

  async function saveDisplay(next: boolean) {
    savingDisplay = true
    displayMsg = ''
    displayKind = ''
    try {
      const res = await patchSettings({ display_en: next })
      displayOn = next
      displayKind = 'ok'
      displayMsg = res.reboot_required ? 'Saved — reboot to apply' : 'Saved'
    } catch (e) {
      displayOn = saved?.display_en ?? !next
      displayKind = 'err'
      displayMsg = (e as Error).message
    } finally {
      savingDisplay = false
    }
  }

  async function saveOtaSkip(next: boolean) {
    savingOtaSkip = true
    otaMsg = ''
    otaKind = ''
    try {
      const res = await patchSettings({ ota_skip_check: next })
      otaSkip = next
      otaKind = 'ok'
      otaMsg = res.reboot_required ? 'Saved — reboot to apply' : 'Saved'
    } catch (e) {
      otaSkip = saved?.ota_skip_check ?? !next
      otaKind = 'err'
      otaMsg = (e as Error).message
    } finally {
      savingOtaSkip = false
    }
  }

  async function saveMdns(next: boolean) {
    savingMdns = true
    mdnsMsg = ''
    mdnsKind = ''
    try {
      const res = await patchSettings({ mdns_en: next })
      mdnsOn = next
      mdnsKind = 'ok'
      mdnsMsg = res.reboot_required ? 'Saved — reboot to apply' : 'Saved'
      // When mdns turns off, also reset knot locally (server forces it off)
      if (!next) {
        knotOn = false
      }
    } catch (e) {
      mdnsOn = saved?.mdns_en ?? !next
      mdnsKind = 'err'
      mdnsMsg = (e as Error).message
    } finally {
      savingMdns = false
    }
  }

  async function saveKnot(next: boolean) {
    savingKnot = true
    knotMsg = ''
    knotKind = ''
    try {
      const res = await patchSettings({ knot_en: next })
      knotOn = next
      knotKind = 'ok'
      knotMsg = res.reboot_required ? 'Saved — reboot to apply' : 'Saved'
    } catch (e) {
      knotOn = saved?.knot_en ?? !next
      knotKind = 'err'
      knotMsg = (e as Error).message
    } finally {
      savingKnot = false
    }
  }

  function onDisplayChange(e: Event) {
    saveDisplay((e.currentTarget as HTMLInputElement).checked)
  }

  function onOtaChange(e: Event) {
    // checkbox is "OTA check on boot" — checked = check enabled = skip=false
    saveOtaSkip(!(e.currentTarget as HTMLInputElement).checked)
  }

  function onMdnsChange(e: Event) {
    saveMdns((e.currentTarget as HTMLInputElement).checked)
  }

  function onKnotChange(e: Event) {
    saveKnot((e.currentTarget as HTMLInputElement).checked)
  }

  return {
    get loading() { return loading },
    get loadErr() { return loadErr },
    get displayOn() { return displayOn },
    get otaSkip() { return otaSkip },
    get mdnsOn() { return mdnsOn },
    get knotOn() { return knotOn },
    get savingDisplay() { return savingDisplay },
    get savingOtaSkip() { return savingOtaSkip },
    get savingMdns() { return savingMdns },
    get savingKnot() { return savingKnot },
    get displayMsg() { return displayMsg },
    get otaMsg() { return otaMsg },
    get mdnsMsg() { return mdnsMsg },
    get knotMsg() { return knotMsg },
    get displayKind() { return displayKind },
    get otaKind() { return otaKind },
    get mdnsKind() { return mdnsKind },
    get knotKind() { return knotKind },
    loadSettings,
    saveDisplay,
    saveOtaSkip,
    saveMdns,
    saveKnot,
    onDisplayChange,
    onOtaChange,
    onMdnsChange,
    onKnotChange,
  }
}
