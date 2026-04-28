<script lang="ts">
  import Brand from 'ui-kit/Brand.svelte'
  import { info } from '../lib/stores'
</script>

<Brand title="TaipanMiner">
  {#if $info}
    <div class="sub">{$info.board} · {$info.version}</div>
  {/if}
</Brand>

<style>
  .sub {
    font-size: 12px;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-variant-numeric: tabular-nums;
  }
</style>
