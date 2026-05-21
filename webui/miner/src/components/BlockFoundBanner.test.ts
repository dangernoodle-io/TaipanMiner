import { describe, it, expect, beforeEach, vi } from 'vitest'
import { render, screen, fireEvent } from '@testing-library/svelte'
import { flushSync, tick } from 'svelte'
import BlockFoundBanner from './BlockFoundBanner.svelte'
import { EVENT_BUS_KEY, type EventBus } from '../lib/eventBus.svelte'
import { BLOCK_FOUND_TOPIC } from '../lib/blockFoundState.svelte'

/** Test double: captures subscribers so tests can fire events into the
 *  component's internal state machine. */
function fakeBus(): { bus: EventBus; fire: (topic: string, data: string) => void } {
  const subs = new Map<string, Set<(d: string) => void>>()
  const bus: EventBus = {
    subscribe(topic, fn) {
      let set = subs.get(topic)
      if (!set) {
        set = new Set()
        subs.set(topic, set)
      }
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
  const r = render(BlockFoundBanner, {
    context: new Map([[EVENT_BUS_KEY, bus]]),
  })
  return { ...r, fire }
}

beforeEach(() => {
  localStorage.clear()
})

describe('BlockFoundBanner', () => {
  it('renders nothing when no event has been received', () => {
    const { container } = renderWithBus()
    expect(container.querySelector('.block-found-banner')).toBeNull()
  })

  it('renders banner with host:port after a block.found event arrives', async () => {
    const { fire } = renderWithBus()
    fire(BLOCK_FOUND_TOPIC, JSON.stringify({ host: 'pool.example.com', port: 3333 }))
    flushSync()
    await tick()
    expect(screen.getByText(/Block found/)).toBeInTheDocument()
    expect(screen.getByText(/pool\.example\.com:3333/)).toBeInTheDocument()
  })

  it('renders share_diff when provided', async () => {
    const { fire } = renderWithBus()
    fire(BLOCK_FOUND_TOPIC, JSON.stringify({
      host: 'pool.example.com', port: 3333, share_diff: 9876.54,
    }))
    flushSync()
    await tick()
    expect(screen.getByText(/diff/)).toBeInTheDocument()
  })

  it('does not render diff section when share_diff is absent', async () => {
    const { container, fire } = renderWithBus()
    fire(BLOCK_FOUND_TOPIC, JSON.stringify({ host: 'pool.example.com', port: 3333 }))
    flushSync()
    await tick()
    expect(container.textContent).not.toMatch(/diff/)
  })

  it('hides banner after × is clicked', async () => {
    const { container, fire } = renderWithBus()
    fire(BLOCK_FOUND_TOPIC, JSON.stringify({
      host: 'pool.example.com', port: 3333, share_diff: 42,
    }))
    flushSync()
    await tick()
    expect(container.querySelector('.block-found-banner')).not.toBeNull()

    const btn = screen.getByRole('button', { name: /dismiss/i })
    await fireEvent.click(btn)
    flushSync()
    await tick()
    expect(container.querySelector('.block-found-banner')).toBeNull()
  })
})
