<script lang="ts">
  export let domains: number[]
  export let expected: number | undefined = undefined

  $: maxVal = expected || Math.max(...domains, 1)

  function getColor(val: number, max: number): string {
    if (val === 0) return 'var(--danger)'
    if (max === 0) return 'var(--info)'

    const ratio = val / max
    if (ratio > 0.8) return '#e5ad30'
    if (ratio > 0.6) return '#d9942a'
    if (ratio > 0.4) return '#6b5b95'
    return '#1a5f7a'
  }
</script>

<div class="heatmap">
  {#each domains as domain, i (i)}
    <div class="cell" style="background: {getColor(domain, maxVal)}" title="{domain.toFixed(1)} GH/s">
      <span class="value">{domain.toFixed(0)}</span>
    </div>
  {/each}
</div>

<style>
  .heatmap {
    display: grid;
    grid-template-columns: repeat(4, 1fr);
    gap: 8px;
  }

  .cell {
    aspect-ratio: 1;
    display: flex;
    align-items: center;
    justify-content: center;
    border-radius: 4px;
    transition: transform 0.1s;
    cursor: pointer;
  }

  .cell:hover {
    transform: scale(1.05);
  }

  .value {
    color: var(--bg);
    font-size: 12px;
    font-weight: bold;
  }
</style>
