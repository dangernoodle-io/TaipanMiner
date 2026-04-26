<script lang="ts">
  import { createEventDispatcher } from 'svelte'

  export let open = false
  export let title: string
  export let message: string
  export let confirmLabel = 'Confirm'
  export let cancelLabel = 'Cancel'
  export let danger = false
  /** Optional localStorage key. When set, user can tick "Don't show again" to skip future prompts. */
  export let skipKey: string | null = null

  const dispatch = createEventDispatcher<{ confirm: void; cancel: void }>()

  let dontShowAgain = false

  function confirm() {
    if (skipKey && dontShowAgain) {
      try { localStorage.setItem(skipKey, '1') } catch { /* ignore */ }
    }
    open = false
    dispatch('confirm')
  }

  function cancel() {
    open = false
    dispatch('cancel')
  }

  export function shouldSkip(key: string): boolean {
    try { return localStorage.getItem(key) === '1' } catch { return false }
  }
</script>

{#if open}
  <div class="backdrop" on:click={cancel} role="presentation"></div>
  <div class="dialog" role="dialog" aria-modal="true" aria-labelledby="confirm-title">
    <h3 id="confirm-title">{title}</h3>
    <p>{message}</p>

    {#if skipKey}
      <label class="skip">
        <input type="checkbox" bind:checked={dontShowAgain} />
        Don't show this again
      </label>
    {/if}

    <div class="actions">
      <button class="btn outline" on:click={cancel}>{cancelLabel}</button>
      <button class="btn {danger ? 'danger' : 'primary'}" on:click={confirm} autofocus>{confirmLabel}</button>
    </div>
  </div>
{/if}

<style>
  .backdrop {
    position: fixed;
    inset: 0;
    background: rgba(0, 0, 0, 0.55);
    backdrop-filter: blur(2px);
    z-index: 40;
  }

  .dialog {
    position: fixed;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 20px 22px;
    width: min(420px, calc(100vw - 32px));
    z-index: 41;
    box-shadow: 0 8px 32px rgba(0, 0, 0, 0.5);
  }

  h3 {
    margin: 0 0 10px 0;
    color: var(--text);
    font-size: 15px;
    font-weight: 600;
  }

  p {
    margin: 0 0 16px 0;
    color: var(--label);
    font-size: 13px;
    line-height: 1.5;
  }

  .skip {
    display: flex;
    align-items: center;
    gap: 6px;
    margin-bottom: 16px;
    font-size: 12px;
    color: var(--muted);
    cursor: pointer;
  }

  .actions {
    display: flex;
    justify-content: flex-end;
    gap: 10px;
  }

</style>
