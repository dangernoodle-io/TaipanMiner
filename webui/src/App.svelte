<script lang="ts">
  import { onMount } from 'svelte'
  import { start, stop } from './lib/stores'
  import { route } from './lib/router'
  import AlertBanner from './components/AlertBanner.svelte'
  import Header from './components/Header.svelte'
  import Nav from './components/Nav.svelte'
  import PoolStrip from './components/PoolStrip.svelte'
  import LiveTitle from './components/LiveTitle.svelte'
  import Dashboard from './pages/Dashboard.svelte'
  import System from './pages/System.svelte'
  import Pool from './pages/Pool.svelte'
  import './lib/theme.css'
  import './App.css'

  onMount(() => {
    start()
    return () => stop()
  })
</script>

<LiveTitle />

<main>
  <Header />
  <Nav />
  <AlertBanner />
  <PoolStrip />

  {#if $route === 'system'}
    <System />
  {:else if $route === 'pool'}
    <Pool />
  {:else}
    <Dashboard />
  {/if}
</main>

<style>
  main {
    max-width: 1100px;
    margin: 0 auto;
    padding: 16px;
  }
</style>
