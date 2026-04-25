import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { startRebootRecovery, rebooting, __resetRebootPoll } from './stores'
import { get } from 'svelte/store'

vi.mock('./api', () => ({
  ping: vi.fn(),
}))

import { ping as apiPing } from './api'

describe('stores', () => {
  beforeEach(() => {
    vi.useFakeTimers()
    vi.clearAllMocks()
    vi.clearAllTimers()
    __resetRebootPoll()
    rebooting.set({ active: false, reason: '', elapsed: 0, timedOut: false })
  })

  afterEach(() => {
    vi.useRealTimers()
  })

  describe('startRebootRecovery', () => {
    it('sets rebooting.active to true with reason', () => {
      startRebootRecovery('test-reboot')
      const state = get(rebooting)
      expect(state.active).toBe(true)
      expect(state.reason).toBe('test-reboot')
    })

    it('initializes elapsed and timedOut correctly', () => {
      startRebootRecovery('flash')
      const state = get(rebooting)
      expect(state.elapsed).toBe(0)
      expect(state.timedOut).toBe(false)
    })

    it('sets correct initial state structure', () => {
      startRebootRecovery('update')
      const state = get(rebooting)
      expect(state).toHaveProperty('active')
      expect(state).toHaveProperty('reason')
      expect(state).toHaveProperty('elapsed')
      expect(state).toHaveProperty('timedOut')
    })
  })

  describe('reboot recovery polling lifecycle', () => {
    it('does not poll during the initial 3s pre-wait', async () => {
      vi.mocked(apiPing).mockResolvedValue(false)
      startRebootRecovery('test')
      await vi.advanceTimersByTimeAsync(2999)
      expect(apiPing).not.toHaveBeenCalled()
    })

    it('starts polling 1s after the pre-wait', async () => {
      vi.mocked(apiPing).mockResolvedValue(false)
      startRebootRecovery('test')
      await vi.advanceTimersByTimeAsync(3000)
      await vi.advanceTimersByTimeAsync(1000)
      expect(apiPing).toHaveBeenCalledTimes(1)
      expect(apiPing).toHaveBeenCalledWith(1500)
    })

    it('clears reboot state when ping returns true', async () => {
      vi.mocked(apiPing).mockResolvedValue(true)
      startRebootRecovery('flash')
      await vi.advanceTimersByTimeAsync(3000)
      await vi.advanceTimersByTimeAsync(1000)
      const state = get(rebooting)
      expect(state.active).toBe(false)
    })

    it('stops polling once cleared (no more calls after success)', async () => {
      vi.mocked(apiPing).mockResolvedValue(true)
      startRebootRecovery('test')
      await vi.advanceTimersByTimeAsync(3000)
      await vi.advanceTimersByTimeAsync(1000)
      expect(apiPing).toHaveBeenCalledTimes(1)
      await vi.advanceTimersByTimeAsync(5000)
      expect(apiPing).toHaveBeenCalledTimes(1)
    })

    it('sets timedOut=true after 90s without success', async () => {
      vi.mocked(apiPing).mockResolvedValue(false)
      startRebootRecovery('test')
      await vi.advanceTimersByTimeAsync(3000)
      await vi.advanceTimersByTimeAsync(1000)
      await vi.advanceTimersByTimeAsync(91000)
      const state = get(rebooting)
      expect(state.timedOut).toBe(true)
      expect(state.elapsed).toBeGreaterThan(90)
    })

    it('double startRebootRecovery is a no-op while one is active', async () => {
      vi.mocked(apiPing).mockResolvedValue(false)
      startRebootRecovery('first')
      await vi.advanceTimersByTimeAsync(3000)
      // Now rebootPollId is set by the setTimeout callback
      startRebootRecovery('second')
      const state = get(rebooting)
      expect(state.reason).toBe('first')
    })

    it('elapsed counter increments while polling', async () => {
      vi.mocked(apiPing).mockResolvedValue(false)
      startRebootRecovery('test')
      await vi.advanceTimersByTimeAsync(3000)
      await vi.advanceTimersByTimeAsync(1000)
      let state = get(rebooting)
      expect(state.elapsed).toBeGreaterThanOrEqual(4)
      await vi.advanceTimersByTimeAsync(3000)
      state = get(rebooting)
      expect(state.elapsed).toBeGreaterThanOrEqual(7)
      expect(state.timedOut).toBe(false)
    })
  })
})
