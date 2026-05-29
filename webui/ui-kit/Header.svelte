<script lang="ts">
  import type { Snippet } from 'svelte'
  import Brand from './Brand.svelte'

  let { title, subtitle, right }: {
    title: string
    subtitle?: string
    right?: Snippet
  } = $props()
</script>

<div class="header-row">
  <Brand {title}>
    {#if subtitle}
      <div class="sub">{subtitle}</div>
    {/if}
  </Brand>
  <div class="spacer"></div>
  {#if right}{@render right()}{/if}
</div>

<style>
  .header-row {
    display: flex;
    align-items: center;
    gap: 16px;
    width: 100%;
  }
  .header-row :global(.brand) {
    flex: 0 0 auto;
  }
  .spacer {
    flex: 1 1 auto;
    min-width: 0;
  }
  .sub {
    font-size: 12px;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-variant-numeric: tabular-nums;
  }
</style>
