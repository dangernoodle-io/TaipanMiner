<script lang="ts">
  import { route, type Route } from '../lib/router'

  const links: { id: Route; label: string; disabled?: boolean }[] = [
    { id: 'dashboard',   label: 'Dashboard' },
    { id: 'diagnostics', label: 'Diagnostics' },
    { id: 'history',     label: 'History' },
    { id: 'pool',        label: 'Pool' },
    { id: 'settings',    label: 'Settings' },
    { id: 'system',      label: 'System' },
    { id: 'update',      label: 'Update' }
  ]
</script>

<nav>
  {#each links as link}
    {#if link.disabled}
      <span class="link disabled" title="coming soon">{link.label}</span>
    {:else}
      <a
        class="link"
        class:active={$route === link.id}
        href="#/{link.id}"
      >{link.label}</a>
    {/if}
  {/each}
</nav>

<style>
  nav {
    display: flex;
    gap: 0;
    border-bottom: 1px solid var(--border);
    margin-bottom: 16px;
    overflow-x: auto;
  }

  .link {
    flex: 1;
    text-align: center;
    padding: 10px 14px;
    font-size: 12px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    border-bottom: 2px solid transparent;
    cursor: pointer;
    white-space: nowrap;
    text-decoration: none;
    transition: color 0.15s, border-color 0.15s;
  }

  .link:hover:not(.disabled) {
    color: var(--text);
  }

  .link.active {
    color: var(--accent);
    border-bottom-color: var(--accent);
  }

  .link.disabled {
    color: var(--muted);
    cursor: default;
    opacity: 0.4;
  }
</style>
