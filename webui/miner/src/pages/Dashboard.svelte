<script lang="ts">
  import { stats, power, fan, thermal, hasAsic, fanEditOpen } from '../lib/stores'
  import Hero from '../components/Hero.svelte'
  import ChipsCard from '../components/ChipsCard.svelte'
  import StatTile from '../components/StatTile.svelte'
  import EfficiencyTile from '../components/EfficiencyTile.svelte'
  import PoolStrip from '../components/PoolStrip.svelte'

  function openFanEdit() { fanEditOpen.set(true) }

  const chips = $derived($stats?.asic_chips ?? [])
  const expectedPerDomain = $derived(
    $stats?.asic_total_ghs && chips.length
      ? $stats.asic_total_ghs / chips.length / 4
      : undefined
  )
  const pcoreW = $derived($power?.pcore_mw != null ? $power.pcore_mw / 1000 : null)
</script>

<div class="sticky-pool"><PoolStrip /></div>

<div class="grid">
  <section class="card full hero-card">
    <Hero />
  </section>

  {#if $hasAsic && chips.length > 0}
    <div class="full">
      <ChipsCard {chips} expected_per_domain={expectedPerDomain} />
    </div>
  {/if}

  {#if $hasAsic}
    <section class="card full">
      <h3>Performance</h3>
      <div class="tile-grid wide">
        <StatTile label="Input Voltage" value={$power?.vin_mv != null ? $power.vin_mv / 1000 : null}             unit="V"    decimals={2} flag={$power?.vin_low ? 'warn' : null} />
        <StatTile label="Power Draw"    value={pcoreW}                                                           unit="W"    warn={25} danger={35} />
        <StatTile label="ASIC Voltage"  value={$power?.vcore_mv != null ? $power.vcore_mv / 1000 : null}         unit="V"    decimals={2} />
        <StatTile label="ASIC Current"  value={$power?.icore_ma != null ? $power.icore_ma / 1000 : null}         unit="A"    />
        <EfficiencyTile
          now={$power?.efficiency_jth ?? null}
          m1={$power?.efficiency_jth_1m ?? null}
          m10={$power?.efficiency_jth_10m ?? null}
          h1={$power?.efficiency_jth_1h ?? null}
          expected={$power?.expected_efficiency_jth ?? null}
        />
      </div>
    </section>

    <div class="full split-row">
      <section class="card">
        <h3>Tuning</h3>
        <div class="tile-grid tuning-grid">
          <StatTile label="Freq Configured"  value={$stats?.asic_freq_configured_mhz ?? null}                 unit="MHz" />
          <StatTile label="Freq Effective"   value={$stats?.asic_freq_effective_mhz ?? null}                  unit="MHz" />
        </div>
      </section>

      <section class="card">
        <h3>Cooling</h3>
        <div class="tile-grid">
          <StatTile label="Board"     value={$thermal?.board.present ? ($thermal.board.c ?? null) : null}         unit="°C"  warn={60} danger={75} />
          <StatTile label="ASIC"      value={$thermal?.asic.present ? ($thermal.asic.c ?? null) : null}          unit="°C"  warn={70} danger={80} />
          <StatTile label="VR"        value={$power?.vr_temp_c ?? null}                                          unit="°C"  warn={90} danger={105} />
        </div>
      </section>

      <section class="card">
        <h3>
          Fan
          {#if $fan?.autofan && $fan?.pid_input_src}
            <span class="mode-badge" data-mode="auto" title="Autofan PID input source">PID: {$fan.pid_input_src === 'die' ? 'ASIC' : $fan.pid_input_src.toUpperCase()}</span>
          {/if}
          <button class="header-edit" onclick={openFanEdit} title="Edit fan settings">edit</button>
        </h3>
        <div class="tile-grid">
          <StatTile label="Fan Speed" value={$fan?.duty_pct ?? null}       unit="%"   progress={$fan?.duty_pct ?? null} />
          <StatTile label="RPM"       value={$fan?.rpm ?? null}            unit="rpm" />
          <StatTile label="Target"
                    value={$fan?.autofan
                            ? ($fan?.pid_input_src === 'vr' ? $fan?.vr_target_c : $fan?.die_target_c) ?? null
                            : 'manual'}
                    unit={$fan?.autofan ? '°C' : ''} />
        </div>
      </section>
    </div>

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

  .card.hero-card { padding: 0; }

  .split-row {
    display: grid;
    grid-template-columns: 1fr 1fr 1fr;
    gap: 14px;
  }
  @media (max-width: 1100px) {
    .split-row { grid-template-columns: 1fr 1fr; }
  }
  @media (max-width: 720px) {
    .split-row { grid-template-columns: 1fr; }
  }

  /* card h3 typography lives in ui-kit utilities.css; only the bottom gap
     before .tile-grid is page-specific. */
  h3 { margin-bottom: 12px; }

  .tile-grid {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    align-items: flex-start;
    gap: 14px 18px;
  }

  .tile-grid.wide {
    grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
    align-items: start;
    padding-top: 22px;
  }

  .tile-grid.tuning-grid {
    grid-template-columns: repeat(2, minmax(100px, 160px));
    justify-content: center;
  }

  @media (max-width: 720px) {
    .tile-grid {
      grid-template-columns: repeat(auto-fit, minmax(90px, 1fr));
    }
  }

  .header-edit {
    float: right;
    background: transparent;
    border: 1px solid var(--border);
    color: var(--muted);
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    padding: 2px 8px;
    border-radius: 3px;
    cursor: pointer;
    transition: all 0.15s ease;
  }
  .header-edit:hover { color: var(--accent); border-color: var(--accent); }

  /* mode-badge styles live in ui-kit/utilities.css; only positioning here. */
  h3 :global(.mode-badge) {
    margin-left: 8px;
    vertical-align: middle;
  }
</style>
