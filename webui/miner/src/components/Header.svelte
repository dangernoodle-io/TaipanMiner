<script lang="ts">
  import Brand from 'ui-kit/Brand.svelte'
  import { info } from '../lib/stores'
  import UpdateBadgeContainer from './UpdateBadgeContainer.svelte'
</script>

<div class="header-row">
  <Brand title="TaipanMiner">
    {#if $info}
      <div class="sub">{$info.board} · {$info.version}</div>
    {/if}
  </Brand>
  <div class="spacer"></div>
  <UpdateBadgeContainer />
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
