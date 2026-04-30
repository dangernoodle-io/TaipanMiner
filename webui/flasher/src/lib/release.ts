export type ManifestAsset = {
  file: string
  size: number
  sha256: string
}

export type Manifest = {
  tag: string
  publishedAt: string
  assets: Record<string, ManifestAsset>
}

export async function loadManifest(): Promise<Manifest> {
  const res = await fetch('firmware/manifest.json')
  if (!res.ok) {
    if (res.status === 404) {
      throw new Error(
        'Firmware manifest not found. ' +
        'If you built the flasher locally, run `npm run gen:assets` from webui/flasher first.'
      )
    }
    throw new Error(`Failed to load manifest: ${res.status} ${res.statusText}`)
  }
  return res.json()
}

export async function loadAsset(
  asset: ManifestAsset,
  onProgress?: (loaded: number, total: number) => void
): Promise<Uint8Array> {
  const res = await fetch('firmware/' + asset.file)
  if (!res.ok) {
    throw new Error(`Download failed: ${res.status} ${res.statusText}`)
  }

  let bin: Uint8Array
  if (!res.body) {
    const buf = await res.arrayBuffer()
    onProgress?.(buf.byteLength, buf.byteLength)
    bin = new Uint8Array(buf)
  } else {
    const total = asset.size
    const reader = res.body.getReader()
    const chunks: Uint8Array[] = []
    let loaded = 0
    while (true) {
      const { done, value } = await reader.read()
      if (done) break
      chunks.push(value)
      loaded += value.length
      onProgress?.(loaded, total)
    }
    bin = new Uint8Array(loaded)
    let offset = 0
    for (const c of chunks) {
      bin.set(c, offset)
      offset += c.length
    }
  }

  // Verify SHA-256 integrity
  const expected = asset.sha256.toLowerCase()
  const actual = await sha256Hex(bin)
  if (actual !== expected) {
    throw new Error(`Integrity check failed: expected ${expected}, got ${actual}`)
  }

  return bin
}

async function sha256Hex(bytes: Uint8Array): Promise<string> {
  const buf = await crypto.subtle.digest('SHA-256', bytes as BufferSource)
  return [...new Uint8Array(buf)].map(b => b.toString(16).padStart(2, '0')).join('')
}
