import { SseClient } from './sse'

interface UpdateAvailablePayload {
  current?: string
  latest?: string
  download_url?: string
  available?: boolean
  ts?: number
}

export function createUpdateAvailableState() {
  let available = $state(false)
  let current = $state<string | null>(null)
  let latest = $state<string | null>(null)
  let downloadUrl = $state<string | null>(null)

  let sse: SseClient | null = null

  function start() {
    sse = new SseClient({
      url: '/api/events?topic=update.available',
      eventName: 'update.available',
      onMessage: (data: string) => {
        try {
          const p: UpdateAvailablePayload = JSON.parse(data)
          if (p.available !== undefined) available = p.available
          if (p.current !== undefined) current = p.current ?? null
          if (p.latest !== undefined) latest = p.latest ?? null
          if (p.download_url !== undefined) downloadUrl = p.download_url ?? null
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

  return {
    get available() { return available },
    get current() { return current },
    get latest() { return latest },
    get downloadUrl() { return downloadUrl },
    start,
    stop,
  }
}
