<script lang="ts">
  import { onMount } from 'svelte'
  import { start, stop, stats, power, fan, hasAsic } from './lib/stores'
  import AlertBanner from './components/AlertBanner.svelte'
  import Header from './components/Header.svelte'
  import PoolStrip from './components/PoolStrip.svelte'
  import Hero from './components/Hero.svelte'
  import ChipsCard from './components/ChipsCard.svelte'
  import StatTile from './components/StatTile.svelte'
  import SystemCard from './components/SystemCard.svelte'
  import LiveTitle from './components/LiveTitle.svelte'
  import './lib/theme.css'
  import './App.css'

  onMount(() => {
    start()
    return () => stop()
  })

  $: chips = $stats?.asic_chips ?? []
  $: expectedPerDomain = $stats?.asic_total_ghs && chips.length
    ? $stats.asic_total_ghs / chips.length / 4
    : undefined
  $: pcoreW = $power?.pcore_mw != null ? $power.pcore_mw / 1000 : null
  $: expectedGhs =
    $stats?.asic_freq_configured_mhz && $stats?.asic_small_cores && $stats?.asic_count
      ? ($stats.asic_freq_configured_mhz * $stats.asic_small_cores * $stats.asic_count) / 1000
      : null
</script>

<LiveTitle />

<main>
  <Header />
  <AlertBanner />
  <PoolStrip />

  <div class="grid">
    <section class="card full">
      <Hero />
    </section>

    {#if $hasAsic && chips.length > 0}
      <div class="full">
        <ChipsCard {chips} expected_per_domain={expectedPerDomain} />
      </div>
    {/if}

    {#if $hasAsic}
      <section class="card">
        <h3>Heat</h3>
        <div class="tile-grid">
          <StatTile label="ASIC"  value={$stats?.asic_temp_c ?? null}     unit="°C" warn={70} danger={80} />
          <StatTile label="Board" value={$power?.board_temp_c ?? null}    unit="°C" warn={60} danger={75} />
          <StatTile label="VR"    value={$power?.vr_temp_c ?? null}       unit="°C" warn={75} danger={90} />
        </div>
      </section>

      <section class="card">
        <h3>Fan</h3>
        <div class="tile-grid">
          <StatTile label="RPM"  value={$fan?.rpm ?? null}      unit="rpm" />
          <StatTile label="Duty" value={$fan?.duty_pct ?? null} unit="%"   />
        </div>
        {#if $fan?.duty_pct != null}
          <div class="duty-bar"><div class="duty-fill" style="width: {$fan.duty_pct}%"></div></div>
        {/if}
      </section>

      <section class="card">
        <h3>Power</h3>
        <div class="tile-grid">
          <StatTile label="Core"       value={pcoreW}                                                           unit="W"   warn={25} danger={35} />
          <StatTile label="Vcore"      value={$power?.vcore_mv != null ? $power.vcore_mv / 1000 : null}         unit="V"   />
          <StatTile label="Icore"      value={$power?.icore_ma != null ? $power.icore_ma / 1000 : null}         unit="A"   />
          <StatTile label="Efficiency" value={$power?.efficiency_jth ?? null}                                   unit="J/TH"/>
          <StatTile label="Input"      value={$power?.vin_mv != null ? $power.vin_mv / 1000 : null}             unit="V"   />
        </div>
      </section>

      <section class="card">
        <h3>Performance</h3>
        <div class="tile-grid">
          <StatTile label="Freq cfg"  value={$stats?.asic_freq_configured_mhz ?? null} unit="MHz" />
          <StatTile label="Freq eff"  value={$stats?.asic_freq_effective_mhz ?? null}  unit="MHz" />
          <StatTile label="Expected"  value={expectedGhs}                              unit="GH/s"/>
          <StatTile label="Cores"     value={$stats?.asic_small_cores ?? null}         />
          <StatTile label="Chips"     value={$stats?.asic_count ?? null}               />
        </div>
      </section>
    {/if}

    {#if !$hasAsic && $stats}
      <section class="card">
        <h3>Device</h3>
        <div class="tile-grid">
          <StatTile label="Die temp" value={$stats.temp_c}             unit="°C"    warn={75} danger={85} />
          <StatTile label="Rate"     value={$stats.hw_hashrate / 1000} unit="kH/s"  />
          <StatTile label="Shares"   value={$stats.hw_shares}                       />
        </div>
      </section>
    {/if}

    <div class="full">
      <SystemCard />
    </div>
  </div>
</main>

<style>
  main {
    max-width: 1100px;
    margin: 0 auto;
    padding: 16px;
  }

  .grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
    gap: 14px;
  }

  .full {
    grid-column: 1 / -1;
  }

  .card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 16px;
  }

  .card.full {
    padding: 0;
  }

  h3 {
    margin: 0 0 12px 0;
    font-size: 12px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    font-weight: 600;
  }

  .tile-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(90px, 1fr));
    gap: 14px 18px;
  }

  .duty-bar {
    height: 3px;
    background: var(--border);
    border-radius: 2px;
    overflow: hidden;
    margin-top: 12px;
  }

  .duty-fill {
    height: 100%;
    background: var(--accent);
    transition: width 0.5s ease;
  }
</style>
