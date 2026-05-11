import { describe, it, expect, vi, beforeEach, afterEach } from 'vitest'

// Mock api module
vi.mock('./api', () => ({
  fetchPool: vi.fn(),
  putPool: vi.fn(),
  switchPool: vi.fn(),
  deletePoolSlot: vi.fn(),
}))

// Mock stores module — expose a writable pool store
vi.mock('./stores', async () => {
  const { writable } = await import('svelte/store')
  const pool = writable(null)
  return { pool }
})

import * as api from './api'
import { pool } from './stores'
import { get } from 'svelte/store'
import { createPoolState } from './poolState.svelte'

const basePool = {
  host: 'pool.example.com',
  port: 3333,
  worker: 'worker1',
  wallet: 'wallet1',
  connected: true,
  session_start_ago_s: 10,
  current_difficulty: 1024,
  pool_effective_hashrate: 1e12,
  pool_effective_hashrate_1m: 1e12,
  pool_effective_hashrate_10m: 1e12,
  pool_effective_hashrate_1h: 1e12,
  latency_ms: 50,
  extranonce1: 'abc123',
  extranonce2_size: 4,
  version_mask: '00000000',
  notify: null,
  active_pool_idx: 0 as const,
  extranonce_subscribe_status: 'active' as const,
  configured: {
    primary: {
      host: 'pool.example.com',
      port: 3333,
      worker: 'worker1',
      wallet: 'wallet1',
      extranonce_subscribe: true,
      decode_coinbase: true,
    },
    fallback: {
      host: 'fallback.example.com',
      port: 3334,
      worker: 'worker2',
      wallet: 'wallet2',
      extranonce_subscribe: false,
      decode_coinbase: false,
    },
  },
}

beforeEach(() => {
  vi.clearAllMocks()
  pool.set(null)
  vi.useFakeTimers()
  vi.mocked(api.fetchPool).mockResolvedValue({ ...basePool, session_start_ago_s: 1 } as any)
  vi.mocked(api.putPool).mockResolvedValue(undefined)
  vi.mocked(api.switchPool).mockResolvedValue(undefined)
  vi.mocked(api.deletePoolSlot).mockResolvedValue(undefined)
})

afterEach(() => {
  vi.useRealTimers()
})

describe('createPoolState — initial state', () => {
  it('starts with null editingIdx', () => {
    const state = createPoolState()
    expect(state.editingIdx).toBeNull()
  })

  it('starts with default form values', () => {
    const state = createPoolState()
    expect(state.form.host).toBe('')
    expect(state.form.port).toBe(0)
    expect(state.form.wallet).toBe('')
    expect(state.form.worker).toBe('')
    expect(state.form.pool_pass).toBe('')
    expect(state.form.extranonce_subscribe).toBe(false)
    expect(state.form.decode_coinbase).toBe(true)
  })

  it('starts not saving or switching', () => {
    const state = createPoolState()
    expect(state.saving).toBe(false)
    expect(state.switching).toBe(false)
    expect(state.reconnecting).toBe(false)
  })

  it('starts with empty saveMsg', () => {
    const state = createPoolState()
    expect(state.saveMsg).toBe('')
  })

  it('starts with null frozenPool', () => {
    const state = createPoolState()
    expect(state.frozenPool).toBeNull()
  })

  it('starts with closed remove dialog', () => {
    const state = createPoolState()
    expect(state.removeConfirmOpen).toBe(false)
    expect(state.pendingRemoveSlot).toBeNull()
    expect(state.removeConfirmMsg).toBe('')
  })
})

describe('startEdit', () => {
  it('sets editingIdx and populates form from primary slot', async () => {
    pool.set(basePool as any)
    const state = createPoolState()
    await state.startEdit(0)
    expect(state.editingIdx).toBe(0)
    expect(state.form.host).toBe('pool.example.com')
    expect(state.form.port).toBe(3333)
    expect(state.form.worker).toBe('worker1')
    expect(state.form.wallet).toBe('wallet1')
    expect(state.form.pool_pass).toBe('')
    expect(state.form.extranonce_subscribe).toBe(true)
    expect(state.form.decode_coinbase).toBe(true)
  })

  it('sets editingIdx and populates form from fallback slot', async () => {
    pool.set(basePool as any)
    const state = createPoolState()
    await state.startEdit(1)
    expect(state.editingIdx).toBe(1)
    expect(state.form.host).toBe('fallback.example.com')
    expect(state.form.port).toBe(3334)
    expect(state.form.extranonce_subscribe).toBe(false)
    expect(state.form.decode_coinbase).toBe(false)
  })

  it('uses default form when slot is not configured', async () => {
    pool.set({ ...basePool, configured: { primary: null, fallback: null } } as any)
    const state = createPoolState()
    await state.startEdit(0)
    expect(state.form.host).toBe('')
    expect(state.form.port).toBe(0)
  })

  it('clears saveMsg on startEdit', async () => {
    pool.set(basePool as any)
    const state = createPoolState()
    state.saveMsg = 'old message'
    await state.startEdit(0)
    expect(state.saveMsg).toBe('')
  })
})

describe('cancelEdit', () => {
  it('resets editingIdx to null', async () => {
    pool.set(basePool as any)
    const state = createPoolState()
    await state.startEdit(0)
    state.cancelEdit()
    expect(state.editingIdx).toBeNull()
  })

  it('clears saveMsg', async () => {
    pool.set(basePool as any)
    const state = createPoolState()
    state.saveMsg = 'some message'
    state.cancelEdit()
    expect(state.saveMsg).toBe('')
  })
})

describe('handleSave — non-active slot', () => {
  it('calls putPool with correct body and sets saveMsg=Saved.', async () => {
    // active_pool_idx=1 means editing idx=0 is non-active
    pool.set({ ...basePool, active_pool_idx: 1 } as any)
    const state = createPoolState()
    await state.startEdit(0)
    state.form = { ...state.form, host: 'new.pool.com', port: 4444 }

    const promise = state.handleSave()
    await vi.runAllTimersAsync()
    await promise

    expect(api.putPool).toHaveBeenCalledWith(expect.objectContaining({
      primary: expect.objectContaining({ host: 'new.pool.com', port: 4444 }),
    }))
    expect(state.saveMsg).toBe('Saved.')
    expect(state.editingIdx).toBeNull()
    expect(state.saving).toBe(false)
  })

  it('sets saveMsg with error prefix on putPool failure', async () => {
    pool.set({ ...basePool, active_pool_idx: 1 } as any)
    vi.mocked(api.putPool).mockRejectedValueOnce(new Error('pool put failed: 500'))
    const state = createPoolState()
    await state.startEdit(0)

    const promise = state.handleSave()
    await vi.runAllTimersAsync()
    await promise

    expect(state.saveMsg).toBe('Save failed: pool put failed: 500')
    expect(state.saving).toBe(false)
  })

  it('does nothing when editingIdx is null', async () => {
    const state = createPoolState()
    await state.handleSave()
    expect(api.putPool).not.toHaveBeenCalled()
  })
})

describe('handleSave — active slot (triggers reconnect)', () => {
  it('sets reconnecting and frozenPool during save of active slot', async () => {
    // active_pool_idx=0, connected=true — editing active slot
    pool.set(basePool as any)
    vi.mocked(api.fetchPool).mockResolvedValue({ ...basePool, session_start_ago_s: 1 } as any)
    const state = createPoolState()
    await state.startEdit(0)

    const promise = state.handleSave()
    // Right after call, before timers advance: reconnecting should be true
    expect(state.saving).toBe(true)

    await vi.runAllTimersAsync()
    await promise

    expect(state.saveMsg).toBe('Saved.')
    expect(state.reconnecting).toBe(false)
    expect(state.frozenPool).toBeNull()
    expect(api.fetchPool).toHaveBeenCalled()
  })

  it('still resolves when session never refreshes within deadline', async () => {
    pool.set(basePool as any)
    // fetchPool always returns old age (100 > preAge 10)
    vi.mocked(api.fetchPool).mockResolvedValue({ ...basePool, session_start_ago_s: 100, connected: true } as any)
    const state = createPoolState()
    await state.startEdit(0)

    const promise = state.handleSave()
    // Advance well past 15s deadline
    await vi.advanceTimersByTimeAsync(20000)
    await promise

    // Should still resolve cleanly even without a fresh session
    expect(state.saving).toBe(false)
    expect(state.reconnecting).toBe(false)
    expect(state.frozenPool).toBeNull()
  })
})

describe('handleSwitch', () => {
  it('calls switchPool and polls for fresh session', async () => {
    pool.set(basePool as any)
    vi.mocked(api.fetchPool).mockResolvedValue({
      ...basePool,
      active_pool_idx: 1,
      connected: true,
      session_start_ago_s: 1,
    } as any)
    const state = createPoolState()

    const promise = state.handleSwitch(1)
    expect(state.switching).toBe(true)
    expect(state.frozenPool).not.toBeNull() // frozen before switch

    await vi.runAllTimersAsync()
    await promise

    expect(api.switchPool).toHaveBeenCalledWith(1)
    expect(state.switching).toBe(false)
    expect(state.frozenPool).toBeNull()
  })

  it('sets saveMsg on switchPool failure', async () => {
    pool.set(basePool as any)
    vi.mocked(api.switchPool).mockRejectedValueOnce(new Error('switch pool failed: 503'))
    const state = createPoolState()

    const promise = state.handleSwitch(1)
    await vi.runAllTimersAsync()
    await promise

    expect(state.saveMsg).toBe('Switch failed: switch pool failed: 503')
    expect(state.switching).toBe(false)
    expect(state.frozenPool).toBeNull()
  })

  it('resolves when deadline passes without fresh session', async () => {
    pool.set(basePool as any)
    // switchPool succeeds but fetchPool always returns wrong idx
    vi.mocked(api.fetchPool).mockResolvedValue({
      ...basePool,
      active_pool_idx: 0, // never switches to idx=1
      session_start_ago_s: 100,
    } as any)
    const state = createPoolState()

    const promise = state.handleSwitch(1)
    await vi.advanceTimersByTimeAsync(20000)
    await promise

    expect(state.switching).toBe(false)
    expect(state.frozenPool).toBeNull()
  })
})

describe('requestRemove', () => {
  it('opens confirm dialog with primary message', () => {
    const state = createPoolState()
    state.requestRemove('primary')
    expect(state.removeConfirmOpen).toBe(true)
    expect(state.pendingRemoveSlot).toBe('primary')
    expect(state.removeConfirmMsg).toBe('Remove the primary pool? The fallback will be promoted to primary.')
  })

  it('opens confirm dialog with fallback message', () => {
    const state = createPoolState()
    state.requestRemove('fallback')
    expect(state.removeConfirmOpen).toBe(true)
    expect(state.pendingRemoveSlot).toBe('fallback')
    expect(state.removeConfirmMsg).toBe('Remove the fallback pool? Auto-failover will be disabled.')
  })
})

describe('doRemove', () => {
  it('does nothing when pendingRemoveSlot is null', async () => {
    const state = createPoolState()
    await state.doRemove()
    expect(api.deletePoolSlot).not.toHaveBeenCalled()
  })

  it('calls deletePoolSlot and refreshes pool on success', async () => {
    pool.set(basePool as any)
    const state = createPoolState()
    state.requestRemove('fallback')

    const promise = state.doRemove()
    await vi.runAllTimersAsync()
    await promise

    expect(api.deletePoolSlot).toHaveBeenCalledWith('fallback')
    expect(api.fetchPool).toHaveBeenCalled()
    expect(state.saving).toBe(false)
    expect(state.frozenPool).toBeNull()
    expect(state.pendingRemoveSlot).toBeNull()
  })

  it('sets saveMsg on deletePoolSlot failure', async () => {
    pool.set(basePool as any)
    vi.mocked(api.deletePoolSlot).mockRejectedValueOnce(new Error('delete pool fallback failed: 409'))
    const state = createPoolState()
    state.requestRemove('primary')

    const promise = state.doRemove()
    await vi.runAllTimersAsync()
    await promise

    expect(state.saveMsg).toBe('Remove failed: delete pool fallback failed: 409')
    expect(state.saving).toBe(false)
    expect(state.frozenPool).toBeNull()
  })
})

describe('frozenPool snapshot', () => {
  it('is set during switch and cleared after', async () => {
    const p = basePool as any
    pool.set(p)
    vi.mocked(api.fetchPool).mockResolvedValue({
      ...basePool,
      active_pool_idx: 1,
      connected: true,
      session_start_ago_s: 1,
    } as any)
    const state = createPoolState()

    const promise = state.handleSwitch(1)
    // frozenPool should be set synchronously before any awaits
    expect(state.frozenPool).toBe(p)

    await vi.runAllTimersAsync()
    await promise

    expect(state.frozenPool).toBeNull()
  })
})

describe('form binding round-trip', () => {
  it('set form.url and observe state.form', async () => {
    pool.set(basePool as any)
    const state = createPoolState()
    await state.startEdit(0)
    state.form = { ...state.form, host: 'updated.pool.com' }
    expect(state.form.host).toBe('updated.pool.com')
  })

  it('per-field setters update individual fields and the form snapshot', () => {
    const state = createPoolState()
    state.formHost = 'pool.example.com'
    state.formPort = 4444
    state.formWallet = 'bc1qtest'
    state.formWorker = 'rig-1'
    state.formPoolPass = 'x'
    state.formExtranonceSubscribe = true
    state.formDecodeCoinbase = false
    expect(state.form).toEqual({
      host: 'pool.example.com',
      port: 4444,
      wallet: 'bc1qtest',
      worker: 'rig-1',
      pool_pass: 'x',
      extranonce_subscribe: true,
      decode_coinbase: false,
    })
  })

  it('supports assigning entire form object', async () => {
    const state = createPoolState()
    const newForm = {
      host: 'x.com',
      port: 1234,
      wallet: 'w',
      worker: 'wk',
      pool_pass: 'p',
      extranonce_subscribe: true,
      decode_coinbase: false,
    }
    state.form = newForm
    expect(state.form).toEqual(newForm)
  })
})

describe('setter round-trips', () => {
  it('editingIdx setter', () => {
    const state = createPoolState()
    state.editingIdx = 1
    expect(state.editingIdx).toBe(1)
    state.editingIdx = null
    expect(state.editingIdx).toBeNull()
  })

  it('removeConfirmOpen setter', () => {
    const state = createPoolState()
    state.removeConfirmOpen = true
    expect(state.removeConfirmOpen).toBe(true)
    state.removeConfirmOpen = false
    expect(state.removeConfirmOpen).toBe(false)
  })

  it('pendingRemoveSlot setter', () => {
    const state = createPoolState()
    state.pendingRemoveSlot = 'primary'
    expect(state.pendingRemoveSlot).toBe('primary')
    state.pendingRemoveSlot = null
    expect(state.pendingRemoveSlot).toBeNull()
  })
})
