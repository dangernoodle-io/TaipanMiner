<script lang="ts">
  import { onMount, onDestroy } from 'svelte'
  import 'uplot/dist/uPlot.min.css'
  import { createHistoryState } from '../lib/historyState.svelte'

  const hs = createHistoryState()

  let containerEl: HTMLDivElement

  onMount(() => hs.mountChart(containerEl))
  onDestroy(() => hs.destroyChart())
</script>

<div class="page">
  <div class="toolbar">
    <div class="windows">
      {#each hs.WINDOWS as w, i}
        <button
          class="win-btn"
          class:active={hs.windowIdx === i}
          on:click={() => hs.selectWindow(i)}
        >{w.label}</button>
      {/each}
    </div>
  </div>

  {#if hs.count === 0}
    <div class="empty">
      No samples yet. History collects in the background every 5 seconds while the tab is open — come back in a minute or two.
    </div>
  {:else}
    <div class="chart" bind:this={containerEl}></div>
    <div class="hint">
      {hs.count} samples · click a metric in the legend to toggle · drag to zoom · double-click to reset
    </div>
  {/if}
</div>

<style>
  .page {
    display: flex;
    flex-direction: column;
    gap: 16px;
    padding-top: 12px;
  }

  .toolbar {
    display: flex;
    justify-content: space-between;
    align-items: center;
    gap: 16px;
    flex-wrap: wrap;
  }

  .windows {
    display: inline-flex;
    border: 1px solid var(--border);
    border-radius: 4px;
    overflow: hidden;
  }

  .win-btn {
    background: transparent;
    border: none;
    border-right: 1px solid var(--border);
    padding: 6px 12px;
    font-size: 11px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--muted);
    cursor: pointer;
    font-weight: 600;
    transition: background 0.15s, color 0.15s;
  }

  .win-btn:last-child { border-right: none; }

  .win-btn:hover:not(.active) {
    color: var(--text);
    background: var(--input);
  }

  .win-btn.active {
    background: var(--accent);
    color: var(--bg);
  }

  .chart {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 6px;
    padding: 12px;
    min-height: 360px;
  }

  :global(.chart .u-legend) {
    background: transparent;
    color: var(--text);
    font-size: 11px;
  }

  :global(.chart .u-legend .u-value) {
    font-variant-numeric: tabular-nums;
  }

  :global(.chart .u-axis) {
    color: var(--muted);
  }

  .empty {
    padding: 40px 20px;
    text-align: center;
    font-size: 13px;
    color: var(--muted);
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 6px;
  }

  .hint {
    font-size: 11px;
    color: var(--muted);
    text-align: center;
  }
</style>
