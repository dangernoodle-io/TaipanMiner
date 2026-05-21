import { SseClient } from './sse'

/* Svelte context key for the SSE event bus. Children grab the bus via
 * getContext(EVENT_BUS_KEY), subscribe to a topic on mount, return the
 * unsubscribe from onMount's teardown. Symbol guards against typo
 * collisions with other context keys. */
export const EVENT_BUS_KEY = Symbol('taipanminer.eventBus')

type Handler = (data: string) => void

export interface EventBus {
  /** Register a handler for one /api/events topic. Returns unsubscribe. */
  subscribe(topic: string, fn: Handler): () => void
  /** Open the SSE connection and attach all currently-registered topics. */
  start(): void
  /** Tear down the SSE connection. */
  stop(): void
}

/* The bus owns one SseClient over /api/events (no topic filter) and fans
 * events out to subscribed handlers by name. Adding/removing topics
 * tears down + re-creates the SseClient — connection churn happens only
 * when the topic *set* changes, not on every subscribe. */
export function createEventBus(url: string = '/api/events'): EventBus {
  const handlers = new Map<string, Set<Handler>>()
  let client: SseClient | null = null
  let started = false

  function dispatchFor(topic: string): Handler {
    return (data) => handlers.get(topic)?.forEach((fn) => fn(data))
  }

  function rebuildClient(): void {
    client?.destroy()
    const eventHandlers: Record<string, Handler> = {}
    for (const topic of handlers.keys()) {
      eventHandlers[topic] = dispatchFor(topic)
    }
    client = new SseClient({ url, onMessage: () => {}, eventHandlers })
    if (started) client.start()
  }

  return {
    subscribe(topic, fn) {
      let set = handlers.get(topic)
      const newTopic = !set
      if (!set) {
        set = new Set()
        handlers.set(topic, set)
      }
      set.add(fn)
      /* New topic on a running bus = rebuild the client so the named
       * addEventListener fires for this topic. Adding another handler
       * for an already-subscribed topic is a no-op for the wire. */
      if (newTopic && started) rebuildClient()
      return () => {
        set!.delete(fn)
        if (set!.size === 0) handlers.delete(topic)
        /* Don't rebuild on unsubscribe — keep the connection stable.
         * The dispatchFor closure will simply find no handlers and
         * fan-out to nobody. */
      }
    },
    start() {
      if (started) return
      started = true
      rebuildClient()
    },
    stop() {
      started = false
      client?.destroy()
      client = null
    },
  }
}
