<script lang="ts">
  import { pool } from '../lib/stores'

  // Connection indicator: green=connected, red=not connected, gray=unknown.
  $: dotClass =
    $pool == null ? 'unknown'
    : $pool.connected ? 'connected'
    : 'disconnected'
</script>

<div class="pool-strip">
  <div class="left">
    <span class="conn-dot {dotClass}" aria-hidden="true"></span>
    {#if $pool}
      <strong>{$pool.host}:{$pool.port}</strong>
    {:else}
      <strong>—</strong>
    {/if}
  </div>

  <div class="center">
    <strong>{$pool?.worker ?? '—'}</strong>
  </div>

  <div class="right">
    diff <strong>{$pool?.current_difficulty ?? '—'}</strong>
  </div>
</div>

<style>
  .pool-strip {
    display: grid;
    grid-template-columns: 1fr auto 1fr;
    align-items: center;
    gap: 12px;
    padding: 10px 16px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 6px;
    margin-bottom: 16px;
    font-size: 13px;
    color: var(--label);
  }

  .left { justify-self: start; display: flex; align-items: center; gap: 10px; }
  .center { justify-self: center; text-align: center; }
  .right { justify-self: end; text-align: right; }

  strong { color: var(--text); font-weight: 600; }

  .loading {
    color: var(--label);
    font-style: italic;
  }
</style>
