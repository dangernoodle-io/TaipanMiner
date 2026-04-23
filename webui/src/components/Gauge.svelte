<script lang="ts">
  export let label: string
  export let value: number
  export let min: number = 0
  export let max: number
  export let unit: string = ''
  export let nominal: number | undefined = undefined
  export let danger: number | undefined = undefined

  $: percentage = ((value - min) / (max - min)) * 100
  $: isDanger = danger !== undefined && value >= danger
  $: nominalPercentage = nominal !== undefined ? ((nominal - min) / (max - min)) * 100 : null
</script>

<div class="gauge">
  <div class="label" class:danger={isDanger}>
    {label}
  </div>
  <div class="bar-container">
    <div class="bar-bg">
      <div class="bar-fill" style="width: {percentage}%" class:danger={isDanger}></div>
      {#if nominalPercentage !== null}
        <div class="nominal-marker" style="left: {nominalPercentage}%"></div>
      {/if}
    </div>
    <div class="value" class:danger={isDanger}>
      {value.toFixed(1)}{unit}
    </div>
  </div>
</div>

<style>
  .gauge {
    padding: 12px 0;
  }

  .label {
    font-size: 12px;
    color: var(--label);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    margin-bottom: 6px;
  }

  .label.danger {
    color: var(--danger);
  }

  .bar-container {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  .bar-bg {
    flex: 1;
    height: 6px;
    background: var(--input);
    border-radius: 3px;
    position: relative;
    overflow: hidden;
  }

  .bar-fill {
    height: 100%;
    background: var(--accent);
    border-radius: 3px;
    transition: width 0.2s;
  }

  .bar-fill.danger {
    background: var(--danger);
  }

  .nominal-marker {
    position: absolute;
    top: -3px;
    width: 2px;
    height: 12px;
    background: var(--label);
    opacity: 0.6;
  }

  .value {
    font-size: 12px;
    color: var(--text);
    font-weight: bold;
    min-width: 45px;
    text-align: right;
  }

  .value.danger {
    color: var(--danger);
  }
</style>
