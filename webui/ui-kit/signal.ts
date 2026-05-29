// RSSI (dBm) → signal level for the 3-arc WiFi indicator.
//
// Thresholds follow common WiFi quality guidance: -67 dBm is the reliable
// "strong" floor (VoIP/video-grade), -75 dBm is "good", and -85 dBm is the
// weak edge of usability. The UI previously required >= -50 dBm for a full
// indicator, which is unreachable for typical home signals — so a perfectly
// usable -71 dBm AP rendered as a single bar.
export const SIGNAL_STRONG_DBM = -67
export const SIGNAL_GOOD_DBM = -75
export const SIGNAL_WEAK_DBM = -85

// 0 = below usable floor, 1 = weak, 2 = good, 3 = strong.
export function signalBars(rssi: number): 0 | 1 | 2 | 3 {
  if (rssi >= SIGNAL_STRONG_DBM) return 3
  if (rssi >= SIGNAL_GOOD_DBM) return 2
  if (rssi >= SIGNAL_WEAK_DBM) return 1
  return 0
}
