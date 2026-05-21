interface UpdateAvailablePayload {
  current?: string
  latest?: string
  download_url?: string
  available?: boolean
  ts?: number
}

/* Topic name on /api/events. Exported so the SSE multiplexer can route
 * events of this name to handleMessage() without each state machine
 * holding its own EventSource (one connection per topic doesn't scale
 * past a handful of topics on tight-socket boards). */
export const UPDATE_AVAILABLE_TOPIC = 'update.available'

export function createUpdateAvailableState() {
  let available = $state(false)
  let current = $state<string | null>(null)
  let latest = $state<string | null>(null)
  let downloadUrl = $state<string | null>(null)

  function handleMessage(data: string) {
    try {
      const p: UpdateAvailablePayload = JSON.parse(data)
      if (p.available !== undefined) available = p.available
      if (p.current !== undefined) current = p.current ?? null
      if (p.latest !== undefined) latest = p.latest ?? null
      if (p.download_url !== undefined) downloadUrl = p.download_url ?? null
    } catch {
      // malformed payload — ignore
    }
  }

  return {
    get available() { return available },
    get current() { return current },
    get latest() { return latest },
    get downloadUrl() { return downloadUrl },
    handleMessage,
  }
}
