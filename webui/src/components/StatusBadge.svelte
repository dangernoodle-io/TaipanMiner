<script lang="ts">
  export let state: 'ok' | 'warn' | 'err' | 'idle' = 'idle'
  export let label: string
</script>

<span class="badge" data-state={state}>
  <span class="dot"></span>
  <span>{label}</span>
</span>

<style>
  .badge {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    padding: 3px 8px;
    border-radius: 10px;
    font-size: 11px;
    background: var(--bg);
    border: 1px solid var(--border);
    color: var(--label);
    font-variant-numeric: tabular-nums;
  }

  .dot {
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: var(--muted);
  }

  .badge[data-state="ok"] .dot { background: var(--success); }
  .badge[data-state="ok"] { color: var(--text); }

  .badge[data-state="warn"] .dot { background: var(--warning); }
  .badge[data-state="warn"] { color: var(--warning); }

  .badge[data-state="err"] .dot { background: var(--danger); }
  .badge[data-state="err"] { color: var(--danger); }
</style>
