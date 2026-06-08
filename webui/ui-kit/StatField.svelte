<script lang="ts">
  import type { Snippet } from 'svelte'

  // A label + value stat field. Two presentations:
  //   default        — small uppercase label on top, value below (info/field look)
  //   valueTop       — prominent value on top, caption label below (hero metric)
  // Modifiers: mono (monospace value), prominent (larger/bolder value),
  // accent (value in accent colour). Use inside a flex/grid container;
  // text-align: inherit lets the parent control left/right alignment per column.
  let {
    label,
    mono = false,
    valueTop = false,
    prominent = false,
    accent = false,
    title,
    children,
  }: {
    label: string
    mono?: boolean
    valueTop?: boolean
    prominent?: boolean
    accent?: boolean
    title?: string
    children: Snippet
  } = $props()
</script>

<!-- DOM order is always label→value; valueTop flips it visually via
     column-reverse so the value still reads first to assistive tech as a metric. -->
<div class="stat-field" class:value-top={valueTop}>
  <div class="sk">{label}</div>
  <div class="sv" class:mono class:prominent class:accent {title}>{@render children()}</div>
</div>

<style>
  .stat-field {
    min-width: 0;
    display: flex;
    flex-direction: column;
    gap: 2px;
  }
  .stat-field.value-top {
    flex-direction: column-reverse;
    gap: 1px;
  }

  .sk {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    text-align: inherit;
  }
  .stat-field.value-top .sk { font-size: 9px; }

  .sv {
    font-size: 13px;
    color: var(--text);
    font-variant-numeric: tabular-nums;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
    min-width: 0;
    text-align: inherit;
  }
  .sv.mono { font-family: ui-monospace, SFMono-Regular, Menlo, monospace; }
  .sv.prominent { font-size: 15px; font-weight: 600; line-height: 1.1; }
  .sv.accent { color: var(--accent); }
</style>
