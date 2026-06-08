<script lang="ts">
  /**
   * Standardized hover/focus tooltip. Matches the .has-tip/.tip style
   * originally inlined in Pool.svelte. Trigger is keyboard-focusable so the
   * tooltip also opens on focus (touch parity with native `title`).
   *
   * Use `icon` for a self-contained (?) trigger when there's no other
   * element to attach to — e.g. annotating a label.
   */
  export let text: string
  export let icon = false
  export let placement: 'top' | 'bottom' = 'bottom'
  // which edge the popup anchors to — use 'right' when the trigger sits near the
  // right edge so the panel opens leftward and stays on-screen.
  export let align: 'left' | 'right' = 'left'
</script>

<span class="has-tip" class:icon tabindex="0" role="button" aria-label={text}>
  {#if icon}<span class="ico" aria-hidden="true">?</span>{:else}<slot />{/if}
  <span class="tip" class:top={placement === 'top'} class:right={align === 'right'} role="tooltip">{text}</span>
</span>

<style>
  .has-tip {
    position: relative;
    display: inline-flex;
    align-items: center;
    cursor: help;
    outline: none;
  }

  .has-tip.icon {
    width: 14px;
    height: 14px;
    border-radius: 50%;
    border: 1px solid var(--border);
    color: var(--muted);
    justify-content: center;
    font-size: 9px;
    font-weight: 700;
    background: transparent;
    transition: color 0.15s, border-color 0.15s;
  }

  .has-tip.icon:hover, .has-tip.icon:focus { color: var(--accent); border-color: var(--accent); }

  .ico { line-height: 1; }

  .tip {
    position: absolute;
    top: calc(100% + 6px);
    left: 0;
    z-index: 50;
    width: max-content;
    max-width: 280px;
    padding: 8px 10px;
    border-radius: 6px;
    background: var(--bg-elevated, #1f1f1f);
    color: var(--text);
    border: 1px solid var(--border);
    box-shadow: 0 6px 16px rgba(0, 0, 0, 0.35);
    /* standard UI font — don't inherit a mono/other face from the trigger's cell */
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
    font-size: 11px;
    font-weight: 400;
    line-height: 1.45;
    text-transform: none;
    letter-spacing: 0;
    white-space: normal;
    text-align: left;
    opacity: 0;
    pointer-events: none;
    transform: translateY(-2px);
    transition: opacity 80ms ease-out, transform 80ms ease-out;
  }

  .tip.top {
    top: auto;
    bottom: calc(100% + 6px);
  }

  /* anchor to the right edge so the panel opens leftward (stays on-screen near
     the right side of a card) */
  .tip.right {
    left: auto;
    right: 0;
  }

  .has-tip:hover .tip,
  .has-tip:focus-within .tip {
    opacity: 1;
    transform: translateY(0);
    transition-delay: 300ms;
  }
</style>
