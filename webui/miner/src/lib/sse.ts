/**
 * Self-healing EventSource wrapper.
 *
 * Owns the connect/teardown/reconnect lifecycle and stall detection so
 * components only deal with messages and status. Pure TS — no Svelte stores —
 * to avoid reactive loops when used inside `<script>`.
 *
 * Hidden constraint: ESP32 SSE endpoint serves a single client. A leaked
 * EventSource silently holds the slot and produces a permanent "Disconnected"
 * loop until reload, so every code path that drops the connection MUST go
 * through `teardownEs()`.
 */

export type SseStatus = 'connected' | 'connecting' | 'disconnected' | 'external'

export interface SseClientOptions {
  url: string
  onMessage: (data: string) => void
  onOpen?: () => void
  onStatusChange?: (status: SseStatus) => void
  onRetryAtChange?: (at: number | null) => void
  /** Async refinement of post-error status (e.g. probe /status to detect 'external'). */
  resolveErrorStatus?: () => Promise<SseStatus> | SseStatus
  stallThresholdMs?: number
  stallCheckIntervalMs?: number
  retryInitialMs?: number
  retryMaxMs?: number
  /** Override for tests. */
  eventSourceCtor?: typeof EventSource
  /**
   * SSE event name to subscribe to. Default subscribes to the unnamed
   * "message" channel via `onmessage`. When set, uses `addEventListener` for
   * the named event — required for streams that emit `event: <name>` lines
   * (browsers do not deliver those to `onmessage`).
   */
  eventName?: string
  /**
   * Multi-topic dispatch: map of `event: <name>` → handler. Each entry
   * registers via `addEventListener(name, …)` on the underlying EventSource.
   * Lets one connection fan out to multiple state machines so we don't pay
   * a TCP socket per topic. Mutually exclusive with `eventName` / `onMessage`
   * — pick one shape per client.
   */
  eventHandlers?: Record<string, (data: string) => void>
}

const DEFAULTS = {
  stallThresholdMs: 20000,
  stallCheckIntervalMs: 5000,
  retryInitialMs: 3000,
  retryMaxMs: 20000,
}

export class SseClient {
  private es: EventSource | null = null
  private pendingRetry: ReturnType<typeof setTimeout> | null = null
  private stallTimer: ReturnType<typeof setInterval> | null = null
  private retryDelay: number
  private lastMessageAt = 0
  private destroyed = false
  private readonly opts: SseClientOptions & typeof DEFAULTS
  private readonly ES: typeof EventSource

  constructor(opts: SseClientOptions) {
    this.opts = { ...DEFAULTS, ...opts }
    this.ES = opts.eventSourceCtor ?? EventSource
    this.retryDelay = this.opts.retryInitialMs
  }

  start(): void {
    if (this.destroyed) return
    this.cancelPendingRetry()
    this.teardownEs()
    this.setStatus('connecting')
    this.lastMessageAt = Date.now()

    const es = new this.ES(this.opts.url)
    this.es = es
    es.onopen = () => {
      this.setStatus('connected')
      this.lastMessageAt = Date.now()
      this.retryDelay = this.opts.retryInitialMs
      this.opts.onOpen?.()
    }
    const handler = (e: MessageEvent) => {
      this.lastMessageAt = Date.now()
      this.opts.onMessage(e.data)
    }
    if (this.opts.eventHandlers) {
      for (const [name, fn] of Object.entries(this.opts.eventHandlers)) {
        es.addEventListener(name, (e: Event) => {
          this.lastMessageAt = Date.now()
          fn((e as MessageEvent).data)
        })
      }
    } else if (this.opts.eventName) {
      es.addEventListener(this.opts.eventName, handler as EventListener)
    } else {
      es.onmessage = handler
    }
    es.onerror = () => this.handleError()

    if (this.stallTimer === null) {
      this.stallTimer = setInterval(
        () => this.checkStall(),
        this.opts.stallCheckIntervalMs,
      )
    }
  }

  stop(): void {
    this.cancelPendingRetry()
    this.teardownEs()
    if (this.stallTimer !== null) {
      clearInterval(this.stallTimer)
      this.stallTimer = null
    }
    this.setStatus('disconnected')
  }

  destroy(): void {
    this.destroyed = true
    this.stop()
  }

  /** Force an immediate reconnect, bypassing backoff (e.g. tab regained focus). */
  reconnectNow(): void {
    this.retryDelay = this.opts.retryInitialMs
    this.start()
  }

  /** True if the stream has been silent past the stall threshold. */
  isStale(): boolean {
    return Date.now() - this.lastMessageAt > this.opts.stallThresholdMs
  }

  private handleError(): void {
    this.teardownEs()
    if (this.destroyed) return
    this.setStatus('disconnected')
    if (this.opts.resolveErrorStatus) {
      Promise.resolve()
        .then(() => this.opts.resolveErrorStatus!())
        .then((s) => {
          if (!this.destroyed) this.setStatus(s)
        })
        .catch(() => {})
    }
    this.scheduleRetry()
  }

  private setStatus(s: SseStatus): void {
    this.opts.onStatusChange?.(s)
  }

  private setNextRetryAt(at: number | null): void {
    this.opts.onRetryAtChange?.(at)
  }

  private teardownEs(): void {
    if (this.es) {
      this.es.onopen = null
      this.es.onmessage = null
      this.es.onerror = null
      this.es.close()
      this.es = null
    }
  }

  private cancelPendingRetry(): void {
    if (this.pendingRetry !== null) {
      clearTimeout(this.pendingRetry)
      this.pendingRetry = null
    }
    this.setNextRetryAt(null)
  }

  private scheduleRetry(): void {
    this.cancelPendingRetry()
    const at = Date.now() + this.retryDelay
    this.setNextRetryAt(at)
    this.pendingRetry = setTimeout(() => {
      this.pendingRetry = null
      this.setNextRetryAt(null)
      this.start()
    }, this.retryDelay)
    this.retryDelay = Math.min(this.opts.retryMaxMs, this.retryDelay * 2)
  }

  private checkStall(): void {
    if (!this.es || this.es.readyState !== EventSource.OPEN) return
    // Filtered streams (eventName set) may legitimately receive no data for
    // hours — heartbeat `: ping` comments keep TCP alive but the browser
    // EventSource doesn't surface them, so lastMessageAt never advances.
    // Trust EventSource.readyState + onerror for those; skip stall detection.
    if (this.opts.eventName) return
    if (Date.now() - this.lastMessageAt > this.opts.stallThresholdMs) {
      this.teardownEs()
      this.setStatus('disconnected')
      this.scheduleRetry()
    }
  }
}
