<script lang="ts">
  import logoSvg from '/logo.svg?raw'
  import { info } from '../lib/stores'
</script>

<header class="brand">
  <div class="logo">{@html logoSvg}</div>
  <div class="title">
    <h1>TaipanMiner</h1>
    {#if $info}
      <div class="sub">{$info.board} · {$info.version}</div>
    {/if}
  </div>
</header>

<style>
  .brand {
    display: flex;
    align-items: center;
    gap: 18px;
    padding: 4px 4px 24px;
    color: var(--accent);
  }

  .logo {
    width: 72px;
    height: 72px;
    flex-shrink: 0;
    display: flex;
  }

  .logo :global(svg) {
    width: 100%;
    height: 100%;
  }

  .title {
    display: flex;
    flex-direction: column;
    gap: 4px;
  }

  h1 {
    color: var(--accent);
    font-size: 32px;
    font-weight: 600;
    margin: 0;
    line-height: 1;
    letter-spacing: 0.3px;
  }

  .sub {
    font-size: 12px;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-variant-numeric: tabular-nums;
  }
</style>
