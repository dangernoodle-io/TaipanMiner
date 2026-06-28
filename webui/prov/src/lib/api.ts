export type AccessPoint = { ssid: string; rssi: number; secure: boolean }

export async function fetchScan(): Promise<AccessPoint[]> {
  const r = await fetch('/api/scan', { method: 'POST' })
  if (!r.ok) throw new Error(`scan failed: ${r.status}`)
  return r.json()
}

export type DeviceInfo = { board: string; version: string }

export async function fetchInfo(): Promise<DeviceInfo> {
  const r = await fetch('/api/info')
  if (!r.ok) throw new Error(`info failed: ${r.status}`)
  const info = await r.json() as { build?: { board?: string; version?: string } }
  return { board: (info.build?.board ?? '').trim(), version: (info.build?.version ?? '').trim() }
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
