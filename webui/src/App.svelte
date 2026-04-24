<script lang="ts">
  import { onMount } from 'svelte'
  import { start, stop } from './lib/stores'
  import { route } from './lib/router'
  import AlertBanner from './components/AlertBanner.svelte'
  import Header from './components/Header.svelte'
  import Nav from './components/Nav.svelte'
  import LiveTitle from './components/LiveTitle.svelte'
  import Dashboard from './pages/Dashboard.svelte'
  import System from './pages/System.svelte'
  import Pool from './pages/Pool.svelte'
  import Update from './pages/Update.svelte'
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
  <div class="sticky-nav"><Nav /></div>
  <AlertBanner />

  {#if $route === 'system'}
    <System />
  {:else if $route === 'pool'}
    <Pool />
  {:else if $route === 'update'}
    <Update />
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

  .sticky-nav {
    position: sticky;
    top: 0;
    z-index: 20;
    background: rgba(26, 26, 46, 0.92);
    backdrop-filter: blur(8px);
    margin: 0 -16px;
    padding: 0 16px;
  }

  .sticky-nav :global(nav) {
    margin-bottom: 0;
  }
</style>
