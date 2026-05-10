import { fetchScan, postSave, type AccessPoint } from './api'
import { validateForm, resolvedSsid } from './wifiSetupHelpers'

export function createWifiSetupState(getOnSaved: () => () => void) {
  let networks = $state<AccessPoint[]>([])
  let scanning = $state(false)
  let scanError = $state<string | null>(null)
  let selectedSsid = $state('')
  let manualSsid = $state('')
  let pass = $state('')
  let showPass = $state(false)
  let hostname = $state('')
  let wallet = $state('')
  let worker = $state('')
  let workerEdited = $state(false)
  let poolHost = $state('')
  let poolPort = $state('')
  let poolPass = $state('')
  let errors = $state<Record<string, string>>({})
  let submitting = $state(false)
  let submitError = $state<string | null>(null)

  async function scan() {
    scanning = true
    scanError = null
    networks = []
    selectedSsid = ''
    manualSsid = ''

    try {
      const aps = await fetchScan()
      networks = aps
      if (aps.length > 0) {
        selectedSsid = aps[0].ssid
      }
    } catch (e) {
      scanError = `Scan failed: ${e instanceof Error ? e.message : 'Unknown error'}`
    } finally {
      scanning = false
    }
  }

  function validate(): boolean {
    const newErrors = validateForm({ selectedSsid, manualSsid, wallet, worker, poolHost, poolPort })
    errors = newErrors
    return Object.keys(newErrors).length === 0
  }

  async function handleSubmit() {
    if (!validate()) return

    submitting = true
    submitError = null

    const ssid = resolvedSsid(selectedSsid, manualSsid)
    const savePromise = postSave({
      ssid,
      pass,
      hostname,
      wallet,
      worker,
      pool_host: poolHost,
      pool_port: poolPort,
      pool_pass: poolPass
    })

    // The device tears down its AP ~500ms after responding, so the fetch may
    // never resolve. Race a short timeout: if the request hasn't errored by
    // then, assume it succeeded and advance the UI. Validation errors (400)
    // typically return in <100ms, so they still surface.
    const TIMEOUT_MS = 1500
    let timedOut = false
    const timeout = new Promise<void>(resolve =>
      setTimeout(() => { timedOut = true; resolve() }, TIMEOUT_MS)
    )

    try {
      await Promise.race([savePromise, timeout])
      if (timedOut) {
        getOnSaved()()
        return
      }
      getOnSaved()()
    } catch (e) {
      submitError = `Save failed: ${e instanceof Error ? e.message : 'Unknown error'}`
      submitting = false
    }
  }

  function syncWorker() {
    if (!workerEdited) worker = hostname
  }

  return {
    get networks() { return networks },
    get scanning() { return scanning },
    get scanError() { return scanError },
    get selectedSsid() { return selectedSsid },
    set selectedSsid(v) { selectedSsid = v },
    get manualSsid() { return manualSsid },
    set manualSsid(v) { manualSsid = v },
    get pass() { return pass },
    set pass(v) { pass = v },
    get showPass() { return showPass },
    set showPass(v) { showPass = v },
    get hostname() { return hostname },
    set hostname(v) { hostname = v; syncWorker() },
    get wallet() { return wallet },
    set wallet(v) { wallet = v },
    get worker() { return worker },
    set worker(v) { workerEdited = true; worker = v },
    get poolHost() { return poolHost },
    set poolHost(v) { poolHost = v },
    get poolPort() { return poolPort },
    set poolPort(v) { poolPort = v },
    get poolPass() { return poolPass },
    set poolPass(v) { poolPass = v },
    get errors() { return errors },
    get submitting() { return submitting },
    get submitError() { return submitError },
    scan,
    validate,
    handleSubmit,
  }
}
