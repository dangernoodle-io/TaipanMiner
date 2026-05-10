import type { Info } from './api'

export type { Info }

export interface RebootingState {
  active: boolean
  [key: string]: unknown
}

export interface OtaInstallState {
  kind: string
  [key: string]: unknown
}

export interface OtaUploadState {
  kind: string
  [key: string]: unknown
}

export function firmwareName(info: Info | null): string {
  return info?.board ? `taipanminer-${info.board}.bin` : 'firmware.bin'
}

export function minerBusy(
  rebooting: RebootingState,
  install: OtaInstallState,
  upload: OtaUploadState,
): boolean {
  return rebooting.active || install.kind === 'ok' || upload.kind === 'ok'
}
