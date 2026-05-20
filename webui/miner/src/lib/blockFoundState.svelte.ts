import { SseClient } from './sse'

const LS_KEY = 'taipanminer.blockFound.dismissedAt'

export interface BlockFoundPayload {
  host: string
  port: number
  share_diff?: number
  receivedAt: number
}

export function createBlockFoundState() {
  const storedDismissed = parseInt(localStorage.getItem(LS_KEY) ?? '0', 10)

  let lastFound = $state<BlockFoundPayload | null>(null)
  let dismissedAt = $state<number>(isNaN(storedDismissed) ? 0 : storedDismissed)

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
    dismissedAt = Date.now()
    localStorage.setItem(LS_KEY, String(dismissedAt))
  }

  return {
    get lastFound() { return lastFound },
    get dismissedAt() { return dismissedAt },
    get visible() {
      return lastFound !== null && lastFound.receivedAt > dismissedAt
    },
    start,
    stop,
    dismiss,
  }
}
