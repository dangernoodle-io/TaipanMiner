import uPlot from 'uplot'
import { get } from 'svelte/store'
import { history, hasAsic } from './stores'
import {
  ALL_METRICS, WINDOWS, DEFAULT_WINDOW_IDX,
  windowFilter, buildSeries, buildOptions,
  type MetricDef, type MetricKey
} from './historyChart'

export function createHistoryState() {
  let windowIdx = $state(DEFAULT_WINDOW_IDX)
  let chartEl = $state.raw<HTMLDivElement | null>(null)
  let plot = $state.raw<uPlot | null>(null)
  let unsub = $state.raw<(() => void) | null>(null)

  // Mirror Svelte store values into rune state so $derived can track them.
  let _hasAsic = $state(get(hasAsic))
  let _history = $state(get(history))

  // Subscribe immediately to keep _hasAsic and _history in sync.
  // These subscriptions persist for the lifetime of the state object.
  hasAsic.subscribe((v) => { _hasAsic = v })
  history.subscribe((v) => { _history = v })

  const metrics = $derived(
    _hasAsic ? ALL_METRICS : ALL_METRICS.filter((m) => !m.asicOnly)
  ) as MetricDef[]

  const defaultOn = $derived(
    _hasAsic
      ? (['total_ghs', 'hw_err_pct'] as MetricKey[])
      : (['total_ghs', 'temp_c'] as MetricKey[])
  )

  const count = $derived(_history.length)

  function getFilteredSamples() {
    const now = Math.floor(Date.now() / 1000)
    const windowSec = WINDOWS[windowIdx].seconds
    return windowFilter(_history, windowSec, now)
  }

  function rebuild() {
    if (!chartEl) return
    const filtered = getFilteredSamples()
    const data = buildSeries(filtered, metrics)

    if (plot) {
      plot.destroy()
      plot = null
    }

    const width = chartEl.clientWidth || 800
    plot = new uPlot(buildOptions(width, metrics, defaultOn), data, chartEl)
  }

  function updateData() {
    if (!plot) { rebuild(); return }
    const filtered = getFilteredSamples()
    plot.setData(buildSeries(filtered, metrics))
  }

  function onResize() {
    if (plot && chartEl) {
      plot.setSize({ width: chartEl.clientWidth, height: 340 })
    }
  }

  function selectWindow(i: number) {
    windowIdx = i
    rebuild()
  }

  function mountChart(el: HTMLDivElement) {
    chartEl = el
    rebuild()
    unsub = history.subscribe(() => updateData())
    window.addEventListener('resize', onResize)
  }

  function destroyChart() {
    plot?.destroy()
    plot = null
    unsub?.()
    unsub = null
    window.removeEventListener('resize', onResize)
  }

  return {
    get windowIdx() { return windowIdx },
    get plot() { return plot },
    get metrics() { return metrics },
    get count() { return count },
    WINDOWS,
    selectWindow,
    mountChart,
    destroyChart,
  }
}
