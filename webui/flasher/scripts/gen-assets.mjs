#!/usr/bin/env node
// Resolves the latest TaipanMiner release and populates public/firmware/ with
// factory binaries for each board. Runs before vite dev/build so the flasher's
// firmware updates reflect the latest release without a CORS proxy at runtime.
// Fixtures mode (--fixtures or VITE_FLASHER_FIXTURES=1) writes 1KB stub bins
// for offline UI iteration.

import { readFileSync, writeFileSync, mkdirSync, statSync, createWriteStream } from 'node:fs'
import { promises as fs } from 'node:fs'
import { createHash } from 'node:crypto'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const boardsPath = resolve(here, '../src/generated/boards.json')
const manifestPath = resolve(here, '../public/firmware/manifest.json')
const firmwareDir = dirname(manifestPath)

// Read boards.json
let boards
try {
  const boardsJson = readFileSync(boardsPath, 'utf8')
  boards = JSON.parse(boardsJson)
} catch (e) {
  console.error(`gen-assets: boards.json not found at ${boardsPath}`)
  console.error('hint: run `pnpm gen:boards` first')
  process.exit(1)
}

// Determine mode
const isFixtures = process.argv.includes('--fixtures') || process.env.VITE_FLASHER_FIXTURES === '1'

if (isFixtures) {
  // Fixtures mode: write 1KB stubs and static manifest
  const stubSize = 1024
  const stubBuf = Buffer.alloc(stubSize, 0)
  const stubHash = createHash('sha256').update(stubBuf).digest('hex')

  mkdirSync(firmwareDir, { recursive: true })

  const assets = {}
  for (const board of boards) {
    const filePath = resolve(firmwareDir, board.factoryAsset)
    writeFileSync(filePath, stubBuf)
    assets[board.id] = {
      file: board.factoryAsset,
      size: stubSize,
      sha256: stubHash
    }
  }

  const manifest = {
    tag: 'fixtures',
    publishedAt: new Date().toISOString(),
    assets
  }

  writeFileSync(manifestPath, JSON.stringify(manifest, null, 2) + '\n')
  console.log(`gen-assets: wrote ${boards.length} assets, tag=fixtures → ${firmwareDir}`)
} else {
  // Network mode: fetch latest release and download assets
  ;(async () => {
    const token = process.env.GITHUB_TOKEN
    const headers = {
      'Accept': 'application/vnd.github+json'
    }
    if (token) {
      headers['Authorization'] = `Bearer ${token}`
    }

    // Fetch latest release
    let release
    try {
      const res = await fetch('https://api.github.com/repos/dangernoodle-io/TaipanMiner/releases/latest', {
        headers
      })
      if (!res.ok) {
        throw new Error(`${res.status} ${res.statusText}`)
      }
      release = await res.json()
    } catch (e) {
      console.error(`gen-assets: failed to resolve latest release: ${e.message}`)
      process.exit(1)
    }

    const tag = release.tag_name
    const publishedAt = release.published_at

    // Check idempotency: manifest exists with matching tag and all files present
    let needsUpdate = true
    try {
      const existing = JSON.parse(readFileSync(manifestPath, 'utf8'))
      if (existing.tag === tag) {
        let allFilesExist = true
        for (const board of boards) {
          const filePath = resolve(firmwareDir, board.factoryAsset)
          const assetInfo = existing.assets[board.id]
          if (!assetInfo) {
            allFilesExist = false
            break
          }
          try {
            const stats = statSync(filePath)
            if (stats.size !== assetInfo.size) {
              allFilesExist = false
              break
            }
          } catch {
            allFilesExist = false
            break
          }
        }
        if (allFilesExist) {
          needsUpdate = false
          console.log(`gen-assets: cached ${tag}, skipping`)
        }
      }
    } catch {
      // manifest doesn't exist or is unparseable, proceed with update
    }

    if (!needsUpdate) {
      return
    }

    // Download assets
    mkdirSync(firmwareDir, { recursive: true })

    const assets = {}
    for (const board of boards) {
      // Find asset by name
      const asset = release.assets.find(a => a.name === board.factoryAsset)
      if (!asset) {
        console.error(`gen-assets: asset ${board.factoryAsset} not found in ${tag}`)
        process.exit(1)
      }

      const filePath = resolve(firmwareDir, board.factoryAsset)
      const downloadUrl = asset.browser_download_url

      // Download and hash in one pass
      try {
        const res = await fetch(downloadUrl, { headers })
        if (!res.ok) {
          throw new Error(`${res.status} ${res.statusText}`)
        }

        const hasher = createHash('sha256')
        const writer = createWriteStream(filePath)
        let totalBytes = 0

        for await (const chunk of res.body) {
          writer.write(chunk)
          hasher.update(chunk)
          totalBytes += chunk.length
        }

        await new Promise((resolve, reject) => {
          writer.end((err) => {
            if (err) reject(err)
            else resolve()
          })
        })

        const sha256 = hasher.digest('hex')
        const sizeKB = (totalBytes / 1024).toFixed(1)
        console.log(`gen-assets: ${board.factoryAsset} (${sizeKB}K) sha256=${sha256}`)

        assets[board.id] = {
          file: board.factoryAsset,
          size: totalBytes,
          sha256
        }
      } catch (e) {
        console.error(`gen-assets: failed to download ${board.factoryAsset}: ${e.message}`)
        process.exit(1)
      }
    }

    // Write manifest
    const manifest = {
      tag,
      publishedAt,
      assets
    }

    writeFileSync(manifestPath, JSON.stringify(manifest, null, 2) + '\n')
    console.log(`gen-assets: wrote ${boards.length} assets, tag=${tag} → ${firmwareDir}`)
  })()
}
