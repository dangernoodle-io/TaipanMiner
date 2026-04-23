<script lang="ts">
  import { type Chip } from '../lib/api'

  export let chips: Chip[]
  export let expected_per_domain: number | undefined = undefined

  function heatColor(value: number, expected: number | undefined): string {
    if (value === 0) return 'var(--danger)'
    if (!expected) return 'var(--accent)'
    const ratio = Math.min(value / expected, 1.15)
    if (ratio < 0.5) return 'var(--warning)'
    if (ratio < 0.85) return 'var(--accent-dim)'
    return 'var(--accent)'
  }
</script>

<section class="card">
  <header>
    <h3>Chips</h3>
    <span class="legend">
      <span class="swatch" style="background: var(--danger)"></span> dead
      <span class="swatch" style="background: var(--warning)"></span> low
      <span class="swatch" style="background: var(--accent)"></span> healthy
    </span>
  </header>

  <div class="chips">
    {#each chips as chip (chip.idx)}
      <div class="chip">
        <div class="chip-head">
          <span class="chip-id">chip {chip.idx}</span>
          <span class="chip-rate">{chip.total_ghs.toFixed(1)} <small>GH/s</small></span>
          <span class="chip-err" class:bad={chip.hw_err_pct > 1}>
            {chip.hw_err_pct.toFixed(2)}% err
          </span>
        </div>
        <div class="domains">
          {#each chip.domain_ghs as d, i}
            <div
              class="domain"
              style="background: {heatColor(d, expected_per_domain)}"
              title="Domain {i}: {d.toFixed(1)} GH/s"
            >
              <span class="d-label">D{i}</span>
              <span class="d-val">{d.toFixed(0)}</span>
            </div>
          {/each}
        </div>
      </div>
    {/each}
  </div>
</section>

<style>
  .card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 16px;
  }

  header {
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    margin-bottom: 12px;
  }

  h3 {
    margin: 0;
    font-size: 12px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    font-weight: 600;
  }

  .legend {
    font-size: 10px;
    color: var(--muted);
    display: flex;
    align-items: center;
    gap: 6px;
  }

  .swatch {
    display: inline-block;
    width: 8px;
    height: 8px;
    border-radius: 2px;
    margin-left: 6px;
  }

  .chips {
    display: flex;
    flex-direction: column;
    gap: 10px;
  }

  .chip + .chip {
    border-top: 1px dashed var(--border);
    padding-top: 10px;
  }

  .chip-head {
    display: grid;
    grid-template-columns: auto 1fr auto;
    align-items: baseline;
    gap: 12px;
    margin-bottom: 6px;
  }

  .chip-id {
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--muted);
  }

  .chip-rate {
    font-size: 16px;
    font-weight: 600;
    color: var(--text);
  }

  .chip-rate small {
    font-size: 11px;
    font-weight: normal;
    color: var(--muted);
    margin-left: 2px;
  }

  .chip-err {
    font-size: 11px;
    color: var(--muted);
  }

  .chip-err.bad {
    color: var(--warning);
  }

  .domains {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 4px;
  }

  .domain {
    border-radius: 3px;
    padding: 4px 6px;
    display: flex;
    align-items: baseline;
    justify-content: space-between;
    color: #000;
    font-size: 10px;
    opacity: 0.92;
    min-height: 20px;
  }

  .d-label {
    font-weight: 600;
    letter-spacing: 0.5px;
  }

  .d-val {
    font-family: ui-monospace, Menlo, monospace;
    font-size: 10px;
  }
</style>
