<script lang="ts">
  import { stats, connected } from '../lib/stores'
</script>

<div class="pool-strip">
  <div class="left">
    {#if $stats}
      <strong>{$stats.pool_host}:{$stats.pool_port}</strong>
    {:else}
      <span class="loading">Loading…</span>
    {/if}
  </div>

  <div class="center">
    {#if $stats}
      <strong>{$stats.worker}</strong>
    {/if}
  </div>

  <div class="right">
    {#if $stats}
      diff <strong>{$stats.pool_difficulty}</strong>
    {/if}
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

  .dot {
    display: inline-block;
    width: 8px;
    height: 8px;
    border-radius: 50%;
    flex-shrink: 0;
  }

  .dot.connected {
    background: var(--success);
    animation: pulse 2s ease-in-out infinite;
  }

  .dot.disconnected {
    background: var(--danger);
  }

  @keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.4; }
  }

  .loading {
    color: var(--label);
    font-style: italic;
  }
</style>
