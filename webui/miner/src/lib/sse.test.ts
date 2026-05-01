import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest'
import { SseClient, type SseStatus } from './sse'

class FakeEventSource {
  static instances: FakeEventSource[] = []
  url: string
  readyState = 0
  onopen: ((e: Event) => void) | null = null
  onmessage: ((e: MessageEvent) => void) | null = null
  onerror: ((e: Event) => void) | null = null
  closed = false
  static OPEN = 1

  constructor(url: string) {
    this.url = url
    FakeEventSource.instances.push(this)
  }

  open() {
    this.readyState = 1
    this.onopen?.(new Event('open'))
  }

  emit(data: string) {
    this.onmessage?.(new MessageEvent('message', { data }))
  }

  fail() {
    this.readyState = 2
    this.onerror?.(new Event('error'))
  }

  close() {
    this.closed = true
    this.readyState = 2
  }
}

// EventSource.OPEN is read in checkStall — make it match the fake.
beforeEach(() => {
  FakeEventSource.instances = []
  vi.useFakeTimers()
  ;(globalThis as unknown as { EventSource: unknown }).EventSource = FakeEventSource
})
afterEach(() => {
  vi.useRealTimers()
})

function lastEs() {
  return FakeEventSource.instances[FakeEventSource.instances.length - 1]
}

describe('SseClient', () => {
  it('opens, reports connected, delivers messages', () => {
    const messages: string[] = []
    const statuses: SseStatus[] = []
    const c = new SseClient({
      url: '/api/logs',
      onMessage: (d) => messages.push(d),
      onStatusChange: (s) => statuses.push(s),
      eventSourceCtor: FakeEventSource as unknown as typeof EventSource,
    })
    c.start()
    expect(statuses).toEqual(['connecting'])
    lastEs().open()
    expect(statuses).toEqual(['connecting', 'connected'])
    lastEs().emit('hello')
    lastEs().emit('world')
    expect(messages).toEqual(['hello', 'world'])
    c.destroy()
  })

  it('schedules a retry with exponential backoff after error', () => {
    const statuses: SseStatus[] = []
    const c = new SseClient({
      url: '/api/logs',
      onMessage: () => {},
      onStatusChange: (s) => statuses.push(s),
      retryInitialMs: 1000,
      retryMaxMs: 8000,
      eventSourceCtor: FakeEventSource as unknown as typeof EventSource,
    })
    c.start()
    lastEs().open()
    lastEs().fail()
    expect(statuses).toContain('disconnected')
    expect(FakeEventSource.instances).toHaveLength(1)

    vi.advanceTimersByTime(1000)
    expect(FakeEventSource.instances).toHaveLength(2)

    // second failure → backoff doubles to 2000
    lastEs().fail()
    vi.advanceTimersByTime(1500)
    expect(FakeEventSource.instances).toHaveLength(2)
    vi.advanceTimersByTime(500)
    expect(FakeEventSource.instances).toHaveLength(3)

    c.destroy()
  })

  it('resets backoff on successful reopen', () => {
    const c = new SseClient({
      url: '/api/logs',
      onMessage: () => {},
      retryInitialMs: 1000,
      retryMaxMs: 8000,
      eventSourceCtor: FakeEventSource as unknown as typeof EventSource,
    })
    c.start()
    lastEs().open()
    lastEs().fail() // schedules retry at 1000, next delay 2000
    vi.advanceTimersByTime(1000)
    lastEs().open() // success → resets retryDelay
    lastEs().fail() // should schedule at 1000 again, not 2000
    vi.advanceTimersByTime(999)
    const before = FakeEventSource.instances.length
    vi.advanceTimersByTime(1)
    expect(FakeEventSource.instances.length).toBe(before + 1)
    c.destroy()
  })

  it('refines status via resolveErrorStatus', async () => {
    const statuses: SseStatus[] = []
    const c = new SseClient({
      url: '/api/logs',
      onMessage: () => {},
      onStatusChange: (s) => statuses.push(s),
      resolveErrorStatus: () => 'external',
      eventSourceCtor: FakeEventSource as unknown as typeof EventSource,
    })
    c.start()
    lastEs().open()
    lastEs().fail()
    // sync default
    expect(statuses[statuses.length - 1]).toBe('disconnected')
    // microtask flush for resolved promise
    await vi.advanceTimersByTimeAsync(0)
    expect(statuses[statuses.length - 1]).toBe('external')
    c.destroy()
  })

  it('emits next-retry timestamp and clears it on retry fire', () => {
    const retryAts: (number | null)[] = []
    const c = new SseClient({
      url: '/api/logs',
      onMessage: () => {},
      onRetryAtChange: (at) => retryAts.push(at),
      retryInitialMs: 1000,
      eventSourceCtor: FakeEventSource as unknown as typeof EventSource,
    })
    c.start()
    lastEs().open()
    lastEs().fail()
    expect(retryAts[retryAts.length - 1]).toBeTypeOf('number')
    vi.advanceTimersByTime(1000)
    expect(retryAts[retryAts.length - 1]).toBeNull()
    c.destroy()
  })

  it('detects stall and forces reconnect', () => {
    const statuses: SseStatus[] = []
    const c = new SseClient({
      url: '/api/logs',
      onMessage: () => {},
      onStatusChange: (s) => statuses.push(s),
      stallThresholdMs: 20000,
      stallCheckIntervalMs: 5000,
      retryInitialMs: 1000,
      eventSourceCtor: FakeEventSource as unknown as typeof EventSource,
    })
    c.start()
    lastEs().open()
    expect(FakeEventSource.instances).toHaveLength(1)
    // checkStall fires every 5s; threshold is 20s. The 20s tick hits the
    // boundary (>) and skips, so the 25s tick is the first that drops.
    vi.advanceTimersByTime(25000)
    expect(statuses).toContain('disconnected')
    // next retry timer fires
    vi.advanceTimersByTime(1000)
    expect(FakeEventSource.instances.length).toBeGreaterThan(1)
    c.destroy()
  })

  it('reconnectNow bypasses backoff', () => {
    const c = new SseClient({
      url: '/api/logs',
      onMessage: () => {},
      retryInitialMs: 5000,
      retryMaxMs: 30000,
      eventSourceCtor: FakeEventSource as unknown as typeof EventSource,
    })
    c.start()
    lastEs().open()
    lastEs().fail() // backoff would now be 5000
    c.reconnectNow()
    expect(FakeEventSource.instances).toHaveLength(2)
    c.destroy()
  })

  it('isStale reflects time since last message', () => {
    const c = new SseClient({
      url: '/api/logs',
      onMessage: () => {},
      stallThresholdMs: 20000,
      eventSourceCtor: FakeEventSource as unknown as typeof EventSource,
    })
    c.start()
    lastEs().open()
    expect(c.isStale()).toBe(false)
    vi.advanceTimersByTime(19999)
    expect(c.isStale()).toBe(false)
    vi.advanceTimersByTime(2)
    expect(c.isStale()).toBe(true)
    c.destroy()
  })

  it('destroy prevents further reconnects', () => {
    const c = new SseClient({
      url: '/api/logs',
      onMessage: () => {},
      retryInitialMs: 1000,
      eventSourceCtor: FakeEventSource as unknown as typeof EventSource,
    })
    c.start()
    lastEs().open()
    lastEs().fail()
    c.destroy()
    vi.advanceTimersByTime(10000)
    expect(FakeEventSource.instances).toHaveLength(1)
  })

  it('stop closes the EventSource', () => {
    const c = new SseClient({
      url: '/api/logs',
      onMessage: () => {},
      eventSourceCtor: FakeEventSource as unknown as typeof EventSource,
    })
    c.start()
    const es = lastEs()
    expect(es.closed).toBe(false)
    c.stop()
    expect(es.closed).toBe(true)
  })
})
