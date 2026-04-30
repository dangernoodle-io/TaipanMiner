#!/usr/bin/env node
// Parses ../../../platformio.ini and emits src/generated/boards.json.
// Runs before vite dev/build so the flasher's board dropdown stays in sync
// with the firmware build matrix without a manual checked-in list.

import { readFileSync, writeFileSync, mkdirSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const iniPath = resolve(here, '../../../platformio.ini')
const outPath = resolve(here, '../src/generated/boards.json')

const ini = readFileSync(iniPath, 'utf8')

const sections = []
{
  let current = null
  for (const raw of ini.split('\n')) {
    const line = raw.replace(/;.*$/, '')
    const header = line.match(/^\s*\[(.+?)\]\s*$/)
    if (header) {
      if (current) sections.push(current)
      current = { name: header[1].trim(), body: '' }
    } else if (current) {
      current.body += line + '\n'
    }
  }
  if (current) sections.push(current)
}

const envSections = sections
  .filter(s => s.name.startsWith('env:'))
  .map(s => ({ id: s.name.slice('env:'.length), body: s.body }))

// Per-board override for esptool-js post-flash reset behavior. true for
// chips whose console connects via the chip's native USB Serial/JTAG
// (e.g. ESP32-S3 native USB CDC); false for chips behind an external
// UART bridge (FTDI/CP210x/CH340) where standard RTS toggle resets EN.
// Default below is true. Add an override entry when adding a board that
// uses an external UART bridge.
const usbOtgOverrides = {
  // 'some-future-board': false,
}

const boards = envSections
  .filter(s => s.id !== 'esp32s3' && s.id !== 'native' && !s.id.endsWith('-debug'))
  .map(s => {
    const asic = (s.body.match(/-DASIC_([A-Z0-9_]+)/) || [, null])[1]
    return {
      id: s.id,
      label: s.id
        .split('-')
        .map(part => /^\d/.test(part) ? part : part[0].toUpperCase() + part.slice(1))
        .join(' '),
      asic,
      factoryAsset: `taipanminer-${s.id}-factory.bin`,
      usbOtg: usbOtgOverrides[s.id] ?? true
    }
  })
  .sort((a, b) => a.id.localeCompare(b.id))

if (boards.length === 0) {
  console.error(`gen-boards: no boards parsed from ${iniPath}`)
  process.exit(1)
}

mkdirSync(dirname(outPath), { recursive: true })
writeFileSync(outPath, JSON.stringify(boards, null, 2) + '\n')
console.log(`gen-boards: wrote ${boards.length} boards → ${outPath}`)
