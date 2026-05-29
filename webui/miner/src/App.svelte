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
  import { createEventBus, EVENT_BUS_KEY } from './lib/eventBus.svelte'
  import { setContext } from 'svelte'
  import BlockFoundBanner from './components/BlockFoundBanner.svelte'
  import Dashboard from './pages/Dashboard.svelte'
  import System from './pages/System.svelte'
  import 'ui-kit/theme.css'
  import 'ui-kit/utilities.css'

  /* One SSE connection multiplexes every /api/events topic. App.svelte
   * owns the bus instance and provides it via context; child components
   * (Header → UpdateBadgeContainer, BlockFoundBanner, any future event
   * consumers) create their own state machines and subscribe on mount.
   * App.svelte doesn't need to know which topics exist — adding a new
   * one is purely additive in the component that consumes it. */
  const bus = createEventBus()
  setContext(EVENT_BUS_KEY, bus)

  onMount(() => {
    start()
    bus.start()
    return () => {
      stop()
      bus.stop()
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
  <BlockFoundBanner />
  <Header />
  <div class="sticky-nav"><Nav /></div>
  <AlertBanner {alerts} />

  {#if $route === 'system'}
    <System />
  {:else if $route === 'pool'}
    {#await import('./pages/Pool.svelte')}
      <div class="page-loading">Loading…</div>
    {:then m}
      {@const Page = m.default}
      <Page />
    {/await}
  {:else if $route === 'update'}
    {#await import('./pages/Update.svelte')}
      <div class="page-loading">Loading…</div>
    {:then m}
      {@const Page = m.default}
      <Page />
    {/await}
  {:else if $route === 'diagnostics'}
    {#await import('./pages/Diagnostics.svelte')}
      <div class="page-loading">Loading…</div>
    {:then m}
      {@const Page = m.default}
      <Page />
    {/await}
  {:else if $route === 'settings'}
    {#await import('./pages/Settings.svelte')}
      <div class="page-loading">Loading…</div>
    {:then m}
      {@const Page = m.default}
      <Page />
    {/await}
  {:else if $route === 'history'}
    {#await import('./pages/History.svelte')}
      <div class="page-loading">Loading…</div>
    {:then m}
      {@const Page = m.default}
      <Page />
    {/await}
  {:else if $route === 'knot'}
    {#await import('./pages/Knot.svelte')}
      <div class="page-loading">Loading…</div>
    {:then m}
      {@const Page = m.default}
      <Page />
    {/await}
  {:else}
    <Dashboard />
  {/if}
</main>

<RebootOverlay />
<FanEditDialog />

<style>
  .page-loading {
    padding: 32px;
    text-align: center;
    color: var(--color-text-muted, #888);
  }

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
