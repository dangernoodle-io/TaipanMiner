<script lang="ts">
  import Header from 'ui-kit/Header.svelte'
  import { formatVersion } from 'ui-kit/version'
  import WifiSetup from './pages/WifiSetup.svelte'
  import Save from './pages/Save.svelte'
  import { fetchInfo, type DeviceInfo } from './lib/api'

  type View = 'setup' | 'saving'
  let view = $state<View>('setup')
  let info = $state<DeviceInfo | null>(null)

  fetchInfo().then(i => { info = i }).catch(() => {})

  let subtitle = $derived.by(() => {
    if (!info) return undefined
    return [info.board, formatVersion(info.version)].filter(Boolean).join(' · ') || undefined
  })

  function onSaved() {
    view = 'saving'
  }
</script>

<main>
  <Header title="TaipanMiner" {subtitle} />

  {#if view === 'setup'}
    <WifiSetup onSaved={onSaved} />
  {:else}
    <Save />
  {/if}
</main>

<style>
  main {
    max-width: 600px;
    margin: 0 auto;
    padding: 2rem 1.5rem 4rem;
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
  }
</style>
