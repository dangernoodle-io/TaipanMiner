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
    es.onmessage = (e: MessageEvent) => {
      this.lastMessageAt = Date.now()
      this.opts.onMessage(e.data)
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
    if (Date.now() - this.lastMessageAt > this.opts.stallThresholdMs) {
      this.teardownEs()
      this.setStatus('disconnected')
      this.scheduleRetry()
    }
  }
}
