import { get } from 'svelte/store'
import { fetchPool, putPool, switchPool, deletePoolSlot, type Pool as PoolState } from './api'
import { pool } from './stores'
import { defaultForm, slotFromCurrent, slotFromForm, waitForFreshSession, type PoolForm } from './poolHelpers'

export function createPoolState() {
  let editingIdx = $state<number | null>(null)
  let formHost = $state('')
  let formPort = $state(0)
  let formWallet = $state('')
  let formWorker = $state('')
  let formPoolPass = $state('')
  let formExtranonceSubscribe = $state(false)
  let formDecodeCoinbase = $state(true)
  let saving = $state(false)
  let saveMsg = $state<string>('')
  let switching = $state(false)
  let reconnecting = $state(false)
  // $state.raw preserves identity for comparisons (plain $state wraps in reactive proxy)
  let frozenPool = $state.raw<PoolState | null>(null)
  let removeConfirmOpen = $state(false)
  let pendingRemoveSlot = $state<'primary' | 'fallback' | null>(null)
  let removeConfirmMsg = $state('')

  function readForm(): PoolForm {
    return {
      host: formHost,
      port: formPort,
      wallet: formWallet,
      worker: formWorker,
      pool_pass: formPoolPass,
      extranonce_subscribe: formExtranonceSubscribe,
      decode_coinbase: formDecodeCoinbase,
    }
  }

  function writeForm(f: PoolForm) {
    formHost = f.host
    formPort = f.port
    formWallet = f.wallet
    formWorker = f.worker
    formPoolPass = f.pool_pass
    formExtranonceSubscribe = f.extranonce_subscribe
    formDecodeCoinbase = f.decode_coinbase
  }

  async function startEdit(idx: number) {
    editingIdx = idx
    saveMsg = ''
    const currentPool = get(pool)
    const cfg = idx === 0 ? currentPool?.configured?.primary : currentPool?.configured?.fallback
    if (cfg) {
      writeForm({
        host: cfg.host,
        port: cfg.port,
        wallet: cfg.wallet,
        worker: cfg.worker,
        pool_pass: '',
        extranonce_subscribe: cfg.extranonce_subscribe ?? false,
        decode_coinbase: cfg.decode_coinbase ?? true,
      })
    } else {
      writeForm(defaultForm())
    }
  }

  function cancelEdit() {
    editingIdx = null
    saveMsg = ''
  }

  async function handleSave() {
    if (editingIdx === null) return
    saveMsg = ''
    saving = true
    /* If editing the currently-active slot, the save needs to drive a
     * fresh stratum session before the user-visible state lines up with
     * what they just entered. Mirror handleSwitch's freeze-and-poll so
     * the page doesn't flicker with stale values during the reconnect. */
    const currentPool = get(pool)
    const editingActive = currentPool?.active_pool_idx === editingIdx && currentPool?.connected
    const preAge = editingActive ? (currentPool?.session_start_ago_s ?? null) : null
    if (editingActive) {
      frozenPool = currentPool
      reconnecting = true
    }
    try {
      const cfg = currentPool?.configured
      const edited = slotFromForm(readForm())
      const body = {
        primary: editingIdx === 0
          ? edited
          : (cfg?.primary ? slotFromCurrent(cfg.primary) : edited),
        fallback: editingIdx === 1
          ? edited
          : (cfg?.fallback ? slotFromCurrent(cfg.fallback) : null),
      }
      await putPool(body)
      saveMsg = 'Saved.'
      editingIdx = null

      if (editingActive) {
        /* Wait for the firmware to bring up a fresh session under the new
         * config. Same shape as handleSwitch — bounded poll, fresh-session
         * detector via session_start_ago_s shrinking. */
        const deadline = Date.now() + 15000
        await waitForFreshSession({
          refresh: async () => { pool.set(await fetchPool()) },
          getSessionAge: () => {
            const p = get(pool)
            if (!p?.connected) return undefined
            return p.session_start_ago_s ?? undefined
          },
          preAge,
          deadlineMs: deadline,
        })
      } else {
        pool.set(await fetchPool())
      }
    } catch (e) {
      saveMsg = `Save failed: ${(e as Error).message}`
    } finally {
      saving = false
      reconnecting = false
      frozenPool = null
    }
  }

  function requestRemove(slot: 'primary' | 'fallback') {
    const isPromote = slot === 'primary'
    removeConfirmMsg = isPromote
      ? 'Remove the primary pool? The fallback will be promoted to primary.'
      : 'Remove the fallback pool? Auto-failover will be disabled.'
    pendingRemoveSlot = slot
    removeConfirmOpen = true
  }

  async function doRemove() {
    if (!pendingRemoveSlot) return
    const slot = pendingRemoveSlot
    pendingRemoveSlot = null
    saveMsg = ''
    saving = true
    // Freeze the view while the firmware reshuffles slots, similar to switch.
    frozenPool = get(pool)
    try {
      await deletePoolSlot(slot)
      pool.set(await fetchPool())
    } catch (e) {
      saveMsg = `Remove failed: ${(e as Error).message}`
    } finally {
      saving = false
      frozenPool = null
    }
  }

  async function handleSwitch(idx: 0 | 1) {
    // Freeze the displayed pool BEFORE flipping `switching` so the first
    // reactive tick already sees a stable view.
    frozenPool = get(pool)
    // Snapshot pre-switch session age. The firmware flips active_pool_idx
    // synchronously and only tears down the stratum socket on the next loop
    // iteration, so checking idx+connected post-call returns true instantly
    // while the *old* session is still up. Wait for a fresh session
    // (new session_start_ago_s smaller than the pre-switch value).
    const preAge = get(pool)?.session_start_ago_s ?? null
    switching = true
    try {
      await switchPool(idx)
      const deadline = Date.now() + 15000
      await waitForFreshSession({
        refresh: async () => { pool.set(await fetchPool()) },
        getSessionAge: () => {
          const p = get(pool)
          if (p?.active_pool_idx !== idx) return undefined
          if (!p?.connected) return undefined
          return p.session_start_ago_s ?? undefined
        },
        preAge,
        deadlineMs: deadline,
      })
    } catch (e) {
      saveMsg = `Switch failed: ${(e as Error).message}`
    } finally {
      switching = false
      frozenPool = null
    }
  }

  return {
    get editingIdx() { return editingIdx },
    set editingIdx(v) { editingIdx = v },
    get form() { return readForm() },
    set form(v) { writeForm(v) },
    get formHost() { return formHost },
    set formHost(v) { formHost = v },
    get formPort() { return formPort },
    set formPort(v) { formPort = v },
    get formWallet() { return formWallet },
    set formWallet(v) { formWallet = v },
    get formWorker() { return formWorker },
    set formWorker(v) { formWorker = v },
    get formPoolPass() { return formPoolPass },
    set formPoolPass(v) { formPoolPass = v },
    get formExtranonceSubscribe() { return formExtranonceSubscribe },
    set formExtranonceSubscribe(v) { formExtranonceSubscribe = v },
    get formDecodeCoinbase() { return formDecodeCoinbase },
    set formDecodeCoinbase(v) { formDecodeCoinbase = v },
    get saving() { return saving },
    get saveMsg() { return saveMsg },
    set saveMsg(v) { saveMsg = v },
    get switching() { return switching },
    get reconnecting() { return reconnecting },
    get frozenPool() { return frozenPool },
    get removeConfirmOpen() { return removeConfirmOpen },
    set removeConfirmOpen(v) { removeConfirmOpen = v },
    get pendingRemoveSlot() { return pendingRemoveSlot },
    set pendingRemoveSlot(v) { pendingRemoveSlot = v },
    get removeConfirmMsg() { return removeConfirmMsg },
    startEdit, cancelEdit, handleSave, handleSwitch, requestRemove, doRemove,
  }
}
