<script lang="ts">
  interface Props {
    available: boolean
    latest: string | null
  }
  let { available, latest }: Props = $props()

  function navigate() {
    window.location.hash = '#/update'
  }
</script>

{#if available}
  <button class="update-badge" onclick={navigate} title="A newer firmware release is available">
    Update available{latest ? ` · ${latest}` : ''}
  </button>
{/if}

<style>
  .update-badge {
    display: inline-flex;
    align-items: center;
    padding: 3px 10px;
    border-radius: 999px;
    background: var(--accent);
    color: #fff;
    font-size: 11px;
    font-weight: 600;
    letter-spacing: 0.4px;
    text-transform: uppercase;
    border: none;
    cursor: pointer;
    white-space: nowrap;
    opacity: 0.92;
    transition: opacity 0.15s;
  }

  .update-badge:hover {
    opacity: 1;
  }
</style>
