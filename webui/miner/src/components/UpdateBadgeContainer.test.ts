import { describe, it, expect, vi } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import { flushSync, tick } from 'svelte'
import UpdateBadgeContainer from './UpdateBadgeContainer.svelte'
import { EVENT_BUS_KEY, type EventBus } from '../lib/eventBus.svelte'
import { UPDATE_AVAILABLE_TOPIC } from '../lib/updateAvailableState.svelte'

function fakeBus(): { bus: EventBus; fire: (topic: string, data: string) => void } {
  const subs = new Map<string, Set<(d: string) => void>>()
  const bus: EventBus = {
    subscribe(topic, fn) {
      let set = subs.get(topic)
      if (!set) { set = new Set(); subs.set(topic, set) }
      set.add(fn)
      return () => set!.delete(fn)
    },
    start: vi.fn(),
    stop: vi.fn(),
  }
  return {
    bus,
    fire(topic, data) {
      subs.get(topic)?.forEach((fn) => fn(data))
    },
  }
}

function renderWithBus() {
  const { bus, fire } = fakeBus()
  const r = render(UpdateBadgeContainer, {
    context: new Map([[EVENT_BUS_KEY, bus]]),
  })
  return { ...r, fire }
}

describe('UpdateBadgeContainer', () => {
  it('renders nothing before any update.available event arrives', () => {
    const { container } = renderWithBus()
    expect(container.querySelector('.update-badge')).toBeNull()
  })

  it('shows the badge after update.available fires with available=true', async () => {
    const { fire } = renderWithBus()
    fire(UPDATE_AVAILABLE_TOPIC, JSON.stringify({ available: true, latest: 'v9.9.9' }))
    flushSync()
    await tick()
    expect(screen.getByText(/Update available.*v9\.9\.9/)).toBeInTheDocument()
  })

  it('hides the badge when available transitions to false', async () => {
    const { container, fire } = renderWithBus()
    fire(UPDATE_AVAILABLE_TOPIC, JSON.stringify({ available: true, latest: 'v9.9.9' }))
    flushSync()
    await tick()
    expect(container.querySelector('.update-badge')).not.toBeNull()
    fire(UPDATE_AVAILABLE_TOPIC, JSON.stringify({ available: false }))
    flushSync()
    await tick()
    expect(container.querySelector('.update-badge')).toBeNull()
  })
})
