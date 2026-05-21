import { SseClient } from './sse'

const LS_KEY = 'taipanminer.blockFound.dismissedKey'

export interface BlockFoundPayload {
  host: string
  port: number
  share_diff?: number
  /* Wall-clock unix seconds from the firmware. 0 = SNTP not synced yet. */
  timestamp?: number
  receivedAt: number
}

/* Stable per-event fingerprint. The browser's EventSource auto-resumes via
 * Last-Event-ID across reloads, so the *same* block.found event re-arrives
 * after a navigation even though the firmware topic is configured non-retained.
 * Tracking dismissal by Date.now() (clock at click time) re-shows the banner
 * because the replayed event's client-side receivedAt is always "now > then".
 * Compare by payload identity instead — a real second block at the same pool
 * has a different share_diff, so collisions are vanishingly unlikely. */
function eventKey(p: { host: string; port: number; share_diff?: number }): string {
  return `${p.host}|${p.port}|${p.share_diff ?? ''}`
}

export function createBlockFoundState() {
  const storedDismissedKey = localStorage.getItem(LS_KEY) ?? ''

  let lastFound = $state<BlockFoundPayload | null>(null)
  let dismissedKey = $state<string>(storedDismissedKey)

  let sse: SseClient | null = null

  function start() {
    sse = new SseClient({
      url: '/api/events?topic=block.found',
      eventName: 'block.found',
      onMessage: (data: string) => {
        try {
          const p = JSON.parse(data)
          lastFound = {
            host: p.host ?? '',
            port: p.port ?? 0,
            share_diff: p.share_diff,
            timestamp: p.timestamp,
            receivedAt: Date.now(),
          }
        } catch {
          // malformed payload — ignore
        }
      },
    })
    sse.start()
  }

  function stop() {
    sse?.destroy()
    sse = null
  }

  function dismiss() {
    if (lastFound) {
      dismissedKey = eventKey(lastFound)
      localStorage.setItem(LS_KEY, dismissedKey)
    }
  }

  return {
    get lastFound() { return lastFound },
    get dismissedKey() { return dismissedKey },
    get visible() {
      return lastFound !== null && eventKey(lastFound) !== dismissedKey
    },
    start,
    stop,
    dismiss,
  }
}
