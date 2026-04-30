<script lang="ts">
  import { stats, power, fan, hasAsic } from '../lib/stores'
  import Hero from '../components/Hero.svelte'
  import ChipsCard from '../components/ChipsCard.svelte'
  import StatTile from '../components/StatTile.svelte'
  import PoolStrip from '../components/PoolStrip.svelte'

  $: chips = $stats?.asic_chips ?? []
  $: expectedPerDomain = $stats?.asic_total_ghs && chips.length
    ? $stats.asic_total_ghs / chips.length / 4
    : undefined
  $: pcoreW = $power?.pcore_mw != null ? $power.pcore_mw / 1000 : null
  $: expectedGhs = $stats?.expected_ghs ?? null
</script>

<div class="sticky-pool"><PoolStrip /></div>

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
        <StatTile label="Fan Speed" value={$fan?.duty_pct ?? null} unit="%"   />
        <StatTile label="RPM"       value={$fan?.rpm ?? null}      unit="rpm" />
      </div>
      {#if $fan?.duty_pct != null}
        <div class="duty-bar"><div class="duty-fill" style="width: {$fan.duty_pct}%"></div></div>
      {/if}
    </section>

    <section class="card">
      <h3>Power</h3>
      <div class="tile-grid">
        <StatTile label="Power Draw"    value={pcoreW}                                                           unit="W"    warn={25} danger={35} />
        <StatTile label="Efficiency"    value={$power?.efficiency_jth ?? null}                                   unit="J/TH" />
        <StatTile label="ASIC Voltage"  value={$power?.vcore_mv != null ? $power.vcore_mv / 1000 : null}         unit="V"    />
        <StatTile label="ASIC Current"  value={$power?.icore_ma != null ? $power.icore_ma / 1000 : null}         unit="A"    />
        <StatTile label="Input Voltage" value={$power?.vin_mv != null ? $power.vin_mv / 1000 : null}             unit="V"    flag={$power?.vin_low ? 'warn' : null} />
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

</div>

<style>
  .sticky-pool {
    position: sticky;
    top: 42px;
    z-index: 15;
    background: rgba(26, 26, 46, 0.92);
    backdrop-filter: blur(8px);
    margin: 0 -16px 14px;
    padding: 8px 16px;
  }

  .sticky-pool :global(.pool-strip) {
    margin-bottom: 0;
  }

  .card.full { padding: 0; }

  h3 {
    margin: 0 0 12px 0;
    font-size: 12px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    font-weight: 600;
  }

  .tile-grid {
    display: flex;
    justify-content: space-between;
    align-items: flex-start;
    gap: 18px;
    flex-wrap: wrap;
  }

  .tile-grid > :global(.tile) { text-align: left; }
  .tile-grid > :global(.tile:last-child) { text-align: right; }

  @media (max-width: 720px) {
    .tile-grid {
      display: grid;
      grid-template-columns: repeat(auto-fit, minmax(90px, 1fr));
      gap: 14px 18px;
    }
    .tile-grid > :global(.tile),
    .tile-grid > :global(.tile:last-child) { text-align: left; }
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
