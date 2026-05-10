export function validateForm(fields: {
  selectedSsid: string
  manualSsid: string
  wallet: string
  worker: string
  poolHost: string
  poolPort: string
}): Record<string, string> {
  const errors: Record<string, string> = {}

  const ssid = fields.selectedSsid === '__manual__' ? fields.manualSsid : fields.selectedSsid
  if (!ssid) {
    errors.ssid = 'Network is required'
  }

  if (!fields.wallet.trim()) {
    errors.wallet = 'Required'
  }

  if (!fields.worker.trim()) {
    errors.worker = 'Required'
  }

  if (!fields.poolHost.trim()) {
    errors.poolHost = 'Required'
  }

  const port = parseInt(fields.poolPort, 10)
  if (!fields.poolPort || isNaN(port) || port < 1 || port > 65535) {
    errors.poolPort = 'Valid port (1–65535) required'
  }

  return errors
}

export function resolvedSsid(selectedSsid: string, manualSsid: string): string {
  return selectedSsid === '__manual__' ? manualSsid : selectedSsid
}
