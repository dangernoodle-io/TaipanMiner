<script lang="ts">
  import { onMount } from 'svelte'
  import { start, stop, stats, connected, power, pool, health } from './lib/stores'
  import { route } from './lib/router'
  import AlertBanner from './components/AlertBanner.svelte'
  import type { Alert } from './components/AlertBanner.svelte'
  import Header from './components/Header.svelte'
  import Nav from './components/Nav.svelte'
  import LiveTitle from './components/LiveTitle.svelte'
  import RebootOverlay from './components/RebootOverlay.svelte'
  import FanEditDialog from './components/FanEditDialog.svelte'
  import { createUpdateAvailableState, UPDATE_AVAILABLE_TOPIC } from './lib/updateAvailableState.svelte'
  import { createBlockFoundState, BLOCK_FOUND_TOPIC } from './lib/blockFoundState.svelte'
  import { SseClient } from './lib/sse'
  import BlockFoundBanner from './components/BlockFoundBanner.svelte'
  import Dashboard from './pages/Dashboard.svelte'
  import System from './pages/System.svelte'
  import Pool from './pages/Pool.svelte'
  import Update from './pages/Update.svelte'
  import Diagnostics from './pages/Diagnostics.svelte'
  import Settings from './pages/Settings.svelte'
  import History from './pages/History.svelte'
  import Knot from './pages/Knot.svelte'
  import 'ui-kit/theme.css'
  import 'ui-kit/utilities.css'

  const updateState = createUpdateAvailableState()
  const blockFoundState = createBlockFoundState()

  /* One SSE connection multiplexes every /api/events topic to the page's
   * state machines. The previous one-client-per-topic shape opened a
   * persistent socket per topic, which scales linearly as new topics
   * land and contributes to socket/select-cb pressure on the firmware
   * (latent lwip asserts have surfaced under enough concurrent SSE
   * clients). New event topics drop into the eventHandlers map without
   * adding sockets. */
  let eventBus: SseClient | null = null

  onMount(() => {
    start()
    eventBus = new SseClient({
      url: '/api/events',
      onMessage: () => {},
      eventHandlers: {
        [UPDATE_AVAILABLE_TOPIC]: updateState.handleMessage,
        [BLOCK_FOUND_TOPIC]: blockFoundState.handleMessage,
      },
    })
    eventBus.start()
    return () => {
      stop()
      eventBus?.destroy()
      eventBus = null
    }
  })

  $: alerts = (() => {
    const list: Alert[] = []
    if ($health?.sha_self_test_failed) {
      list.push({
        key: 'sha_self_test_failed',
        severity: 'danger',
        message: 'SHA self-test failed: Mining is halted. The on-chip SHA-256 hardware or the firmware-side SHA verification produced an incorrect result on the boot known-vector test. This usually indicates a hardware fault or a corrupted firmware image. Reflash from a known-good release; if the failure persists, the device may need to be replaced.'
      })
    }
    if (!$connected) {
      list.push({ key: 'disconnected', severity: 'danger', message: 'Miner unreachable' })
    }
    if ($stats?.asic_temp_c && $stats.asic_temp_c > 75) {
      list.push({ key: 'temp', severity: 'warning', message: `High temperature: ${$stats.asic_temp_c.toFixed(1)}°C` })
    }
    if ($power?.vin_low) {
      list.push({
        key: 'vin_low',
        severity: 'warning',
        message: $power.vin_mv != null
          ? `Input voltage low: ${($power.vin_mv / 1000).toFixed(2)}V`
          : 'Input voltage low'
      })
    }
    if ($pool && !$pool.current_difficulty) {
      list.push({ key: 'pool_diff', severity: 'info', message: 'Waiting for pool difficulty' })
    }
    return list
  })()
</script>

<LiveTitle />

<main>
  <BlockFoundBanner state={blockFoundState} />
  <Header {updateState} />
  <div class="sticky-nav"><Nav /></div>
  <AlertBanner {alerts} />

  {#if $route === 'system'}
    <System />
  {:else if $route === 'pool'}
    <Pool />
  {:else if $route === 'update'}
    <Update />
  {:else if $route === 'diagnostics'}
    <Diagnostics />
  {:else if $route === 'settings'}
    <Settings />
  {:else if $route === 'history'}
    <History />
  {:else if $route === 'knot'}
    <Knot />
  {:else}
    <Dashboard />
  {/if}
</main>

<RebootOverlay />
<FanEditDialog />

<style>
  main {
    max-width: 1100px;
    margin: 0 auto;
    padding: 16px;
  }

  .sticky-nav {
    position: sticky;
    top: 0;
    z-index: 20;
    background: rgba(26, 26, 46, 0.92);
    backdrop-filter: blur(8px);
    margin: 0 -16px 16px;
    padding: 0 16px;
  }

  .sticky-nav :global(nav) {
    margin-bottom: 0;
  }
</style>
