<script lang="ts">
  import type { Snippet } from 'svelte'

  // One label/value row inside an <InfoCard>. Owns the single source of truth
  // for key/value typography so every info card looks identical.
  //   mono  — render the value in the monospace face (same size as proportional)
  //   bad   — flag the value (warning colour)
  //   title — native tooltip on the value (full text when truncated by ellipsis)
  let { label, mono = false, bad = false, title, children }:
    { label: string; mono?: boolean; bad?: boolean; title?: string; children: Snippet } = $props()
</script>

<div class="info-row">
  <dt>{label}</dt>
  <dd class:mono class:bad {title}>{@render children()}</dd>
</div>

<style>
  .info-row {
    display: grid;
    grid-template-columns: 1fr auto;
    gap: 12px;
    align-items: baseline;
    min-width: 0;
    border-bottom: 1px dotted var(--border);
    padding: 6px 0;
  }

  dt {
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-size: 10px;
  }

  dd {
    margin: 0;
    color: var(--text);
    font-size: 12px;
    font-variant-numeric: tabular-nums;
    text-align: right;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  /* monospace values keep the SAME size as proportional ones — this is the
     consistency fix (previously .mono / .small dropped to 11px). */
  dd.mono { font-family: ui-monospace, Menlo, monospace; }
  dd.bad { color: var(--warning); }

  /* secondary text inside a value (e.g. "12 / 12") */
  dd :global(.dim) { color: var(--muted); }
</style>
