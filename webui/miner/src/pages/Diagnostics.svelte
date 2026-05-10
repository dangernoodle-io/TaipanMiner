<script lang="ts">
  import { onMount, onDestroy } from 'svelte'
  import ConfirmDialog from '../components/ConfirmDialog.svelte'
  import { createDiagnosticsState } from '../lib/diagnosticsState.svelte'

  const ds = createDiagnosticsState()

  onMount(() => ds.init())
  onDestroy(() => ds.destroy())
</script>

<div class="page">
  <div class="section">
    <h2>Recent telemetry drops</h2>
    {#if ds.recentDrops.length === 0}
      <p class="muted">No recent drops.</p>
    {:else}
      <table class="drops">
        <thead>
          <tr>
            <th>Age</th>
            <th>Chip</th>
            <th>Kind</th>
            <th>Addr</th>
            <th>GH/s</th>
            <th>Δ</th>
            <th>Elapsed</th>
          </tr>
        </thead>
        <tbody>
          {#each ds.recentDrops as d}
            <tr>
              <td>{d.ts_ago_s}s</td>
              <td>{d.chip}</td>
              <td>{d.kind}{d.kind === 'domain' ? ` ${d.domain}` : ''}</td>
              <td>0x{d.addr.toString(16).padStart(2, '0')}</td>
              <td>{d.ghs.toFixed(1)}</td>
              <td>0x{d.delta.toString(16).padStart(8, '0')}</td>
              <td>{d.elapsed_s.toFixed(3)}s</td>
            </tr>
          {/each}
        </tbody>
      </table>
    {/if}
  </div>

  <div class="section">
    <div class="log-head">
      <h2>Live Logs
        <span class="status" data-state={ds.status}>
          {#if ds.status === 'connected'}Connected
          {:else if ds.status === 'connecting'}Connecting…
          {:else if ds.status === 'external'}External client connected
          {:else}Disconnected {#if ds.retryInS != null}— retrying in {ds.retryInS}s{/if}{/if}
        </span>
      </h2>
    </div>

    <div class="log-controls">
      <input
        class="filter"
        type="search"
        placeholder="Filter…"
        bind:value={ds.filter}
        spellcheck="false"
      />

      <select
        class="sm-select"
        bind:value={ds.selectedTag}
        disabled={ds.levelsLoading || ds.applying || ds.tagLevels.length === 0}
        title="Log tag"
      >
        {#if ds.tagLevels.length === 0}
          <option value="">—</option>
        {:else}
          {#each ds.tagLevels as t}
            <option value={t.tag}>{t.tag}</option>
          {/each}
        {/if}
      </select>
      <select
        class="sm-select"
        value={ds.selectedLevel}
        on:change={ds.onLevelChange}
        disabled={ds.applying || !ds.selectedTag}
        title="Log level"
      >
        {#each ds.availableLevels as lv}
          <option value={lv}>{lv}</option>
        {/each}
      </select>

      <span class="spacer"></span>

      <label class="autoscroll">
        <input type="checkbox" bind:checked={ds.autoscroll} /> auto-scroll
      </label>
      <button class="btn outline sm" on:click={ds.clear} disabled={!ds.lines.length}>Clear</button>
    </div>

    {#if ds.applyMsg}<div class="status-msg" data-kind={ds.applyKind}>{ds.applyMsg}</div>{/if}
    {#if ds.levelsErr}<div class="status-msg" data-kind="err">{ds.levelsErr}</div>{/if}

    <pre class="log-panel" bind:this={ds.panel} on:scroll={ds.onPanelScroll}>{#each ds.filtered as l}{l}
{/each}</pre>
    {#if ds.filter}
      <div class="filter-hint">
        {ds.filtered.length} of {ds.lines.length} lines match
      </div>
    {/if}
  </div>

  <div class="section">
    <h2>Device</h2>
    <button class="btn danger" on:click={ds.requestReboot} disabled={ds.rebooting}>
      {ds.rebooting ? 'Rebooting…' : 'Reboot'}
    </button>
    {#if ds.rebootMsg}<div class="status-msg">{ds.rebootMsg}</div>{/if}
  </div>
</div>

<ConfirmDialog
  bind:open={ds.showRebootDialog}
  title="Reboot device?"
  message="Mining will be interrupted while the device restarts. It should return in about 15 seconds."
  confirmLabel="Reboot"
  danger
  skipKey={ds.REBOOT_SKIP_KEY}
  on:confirm={ds.doReboot}
/>

<style>
  .page {
    display: flex;
    flex-direction: column;
    gap: 32px;
    padding-top: 12px;
  }

  .muted {
    color: var(--text-dim);
    font-style: italic;
  }

  table.drops {
    width: 100%;
    border-collapse: collapse;
    font-size: 12px;
    font-family: ui-monospace, monospace;
  }
  table.drops th,
  table.drops td {
    text-align: left;
    padding: 4px 8px;
    border-bottom: 1px solid var(--border, rgba(255, 255, 255, 0.08));
  }
  table.drops th {
    color: var(--text-dim);
    font-weight: 600;
  }

  h2 {
    color: var(--accent);
    margin: 0 0 14px 0;
    font-size: 14px;
    text-transform: uppercase;
    letter-spacing: 1px;
  }

  input[type="search"], select {
    padding: 8px 10px;
    background: var(--input);
    color: var(--text);
    border: 1px solid var(--border);
    border-radius: 4px;
    font-size: 12px;
    font-family: inherit;
    font-variant-numeric: tabular-nums;
    transition: border-color 0.15s;
  }

  input:focus, select:focus {
    outline: none;
    border-color: var(--accent);
  }

  select { min-width: 110px; }

  .log-head {
    margin-bottom: 12px;
  }

  .log-head h2 {
    margin: 0;
    display: inline-flex;
    align-items: baseline;
    gap: 14px;
  }

  .log-controls {
    display: flex;
    align-items: center;
    gap: 10px;
    font-size: 11px;
    color: var(--muted);
    margin-bottom: 10px;
    flex-wrap: wrap;
  }

  .log-controls .filter {
    flex: 1;
    min-width: 140px;
  }

  .spacer {
    flex: 1;
    min-width: 8px;
  }

  .sm-select {
    padding: 8px 10px;
    font-size: 12px;
  }

  .filter {
    min-width: 140px;
  }

  .status {
    font-size: 11px;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  .status[data-state="connected"] { color: var(--success); }
  .status[data-state="external"] { color: var(--warning); }
  .status[data-state="disconnected"] { color: var(--danger); }

  .autoscroll {
    display: inline-flex;
    align-items: center;
    gap: 4px;
    cursor: pointer;
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  .log-panel {
    height: 420px;
    overflow-y: auto;
    margin: 0;
    padding: 10px 12px;
    background: var(--bg);
    border: 1px solid var(--border);
    border-radius: 6px;
    font-family: ui-monospace, Menlo, monospace;
    font-size: 11px;
    line-height: 1.5;
    color: var(--text);
    white-space: pre-wrap;
    word-break: break-all;
  }

  .filter-hint {
    margin-top: 8px;
    font-size: 10px;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }


  .status-msg {
    margin-top: 10px;
    font-size: 12px;
    color: var(--label);
  }

  .status-msg[data-kind="ok"] { color: var(--success); }
  .status-msg[data-kind="err"] { color: var(--danger); }
</style>
