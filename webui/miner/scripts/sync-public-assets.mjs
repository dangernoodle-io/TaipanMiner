#!/usr/bin/env node
import { copyFileSync, mkdirSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const src = resolve(here, '../../ui-kit/assets/logo.svg')
const dst = resolve(here, '../public/logo.svg')

mkdirSync(dirname(dst), { recursive: true })
copyFileSync(src, dst)
console.log(`sync-public-assets: ${src} -> ${dst}`)
