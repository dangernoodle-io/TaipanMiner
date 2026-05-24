export type AccessPoint = { ssid: string; rssi: number; secure: boolean }

export async function fetchScan(): Promise<AccessPoint[]> {
  const r = await fetch('/api/scan', { method: 'POST' })
  if (!r.ok) throw new Error(`scan failed: ${r.status}`)
  return r.json()
}

export async function fetchVersion(): Promise<string> {
  const r = await fetch('/api/info')
  if (!r.ok) throw new Error(`version failed: ${r.status}`)
  const info = await r.json() as { version?: string }
  return (info.version ?? '').trim()
}

export type SaveBody = {
  ssid: string
  pass: string
  hostname: string
  wallet: string
  worker: string
  pool_host: string
  pool_port: string
  pool_pass: string
}

export async function postSave(body: SaveBody): Promise<void> {
  const data = new URLSearchParams(body as unknown as Record<string, string>)
  const r = await fetch('/save', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: data.toString()
  })
  if (!r.ok) throw new Error(`save failed: ${r.status}`)
}
