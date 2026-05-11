<script lang="ts">
  import { onMount, onDestroy } from 'svelte'
  import ConfirmDialog from '../components/ConfirmDialog.svelte'
  import { createDiagnosticsState } from '../lib/diagnosticsState.svelte'

  const ds = createDiagnosticsState()

  onMount(() => ds.init())
  onDestroy(() => ds.destroy())
</script>

<div class="page">
  <!-- Status strip: one-line summary on the left, actions on the right -->
  <div class="status-strip">
    <div class="strip-items">
      <span class="strip-item" title="Abnormal resets (panics/WDT since last clear)">
        Resets <strong>{ds.abnormalResets ?? '—'}</strong>
      </span>
      <span class="strip-sep">·</span>
      <span class="strip-item" data-state={ds.panic?.available ? 'err' : 'ok'} title={ds.panic?.panic_reason ?? ''}>
        {#if ds.panic?.available}Panic: <strong>{ds.panic.task ?? 'unknown'}</strong>
        {:else if ds.panic?.coredump}Coredump stored
        {:else}No panic{/if}
      </span>
      <span class="strip-sep">·</span>
      <span class="strip-item" title="Free heap (default cap)">
        Heap <strong>{ds.heap ? (ds.heap.default.free / 1024).toFixed(1) + ' KB' : '—'}</strong>
      </span>
    </div>
    <div class="strip-actions">
      <button class="btn danger sm" onclick={ds.requestReboot} disabled={ds.rebooting}>
        {ds.rebooting ? 'Rebooting…' : 'Reboot'}
      </button>
    </div>
  </div>
  {#if ds.rebootMsg}<div class="status-msg" data-kind="err">{ds.rebootMsg}</div>{/if}

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
        onchange={ds.onLevelChange}
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
      <button class="btn outline sm" onclick={ds.clear} disabled={!ds.lines.length}>Clear</button>
    </div>

    {#if ds.applyMsg}<div class="status-msg" data-kind={ds.applyKind}>{ds.applyMsg}</div>{/if}
    {#if ds.levelsErr}<div class="status-msg" data-kind="err">{ds.levelsErr}</div>{/if}

    <pre class="log-panel" bind:this={ds.panel} onscroll={ds.onPanelScroll}>{#each ds.filtered as l}{l}
{/each}</pre>
    {#if ds.filter}
      <div class="filter-hint">
        {ds.filtered.length} of {ds.lines.length} lines match
      </div>
    {/if}
  </div>

  <details class="section disclosure">
    <summary>
      <h2>System health</h2>
      <span class="sum-meta">heap · panic · resets</span>
    </summary>
    <div class="disclosure-body">
      <div class="health-row">
        <div class="health-cell">
          <div class="health-label">Abnormal resets</div>
          <div class="health-value">
            {ds.abnormalResets ?? '—'}
            <button
              class="btn outline xs"
              onclick={ds.doClearAbnormalResets}
              disabled={ds.clearingResets || ds.abnormalResets === 0 || ds.abnormalResets == null}
            >Clear</button>
          </div>
          {#if ds.clearResetsMsg}<div class="hint">{ds.clearResetsMsg}</div>{/if}
        </div>

        <div class="health-cell">
          <div class="health-label">Heap integrity</div>
          <div class="health-value">
            <span class="status" data-state={ds.heapCheckResult || 'unknown'}>
              {#if ds.heapCheckResult === 'ok'}OK
              {:else if ds.heapCheckResult === 'bad'}CORRUPT
              {:else}—{/if}
            </span>
            <button class="btn outline xs" onclick={ds.runHeapCheck} disabled={ds.heapChecking}>
              {ds.heapChecking ? 'Checking…' : 'Check'}
            </button>
          </div>
        </div>

        <button class="btn outline sm refresh-inline" onclick={() => { ds.loadHeap(); ds.loadPanic(); ds.loadAbnormalResets() }}>Refresh</button>
      </div>

      {#if ds.heapErr}<div class="status-msg" data-kind="err">{ds.heapErr}</div>{/if}
      {#if ds.heap}
        {@const heapRows = [
          { name: 'internal', cap: ds.heap.internal },
          { name: 'dma', cap: ds.heap.dma },
          { name: 'default', cap: ds.heap.default },
        ]}
        <table class="kv">
          <thead>
            <tr><th>Cap</th><th>Free</th><th>Largest block</th><th>Min ever free</th><th>Allocated</th></tr>
          </thead>
          <tbody>
            {#each heapRows as r}
              <tr>
                <td>{r.name}</td>
                <td>{(r.cap.free / 1024).toFixed(1)} KB</td>
                <td>{(r.cap.largest_free_block / 1024).toFixed(1)} KB</td>
                <td>{(r.cap.minimum_ever_free / 1024).toFixed(1)} KB</td>
                <td>{(r.cap.allocated / 1024).toFixed(1)} KB</td>
              </tr>
            {/each}
          </tbody>
        </table>
      {/if}

      {#if ds.panic && (ds.panic.available || ds.panic.coredump)}
        <div class="panic-block">
          <div class="panic-head">
            <span class="health-label">Last panic</span>
            {#if ds.panic.available}
              <span class="panic-task">{ds.panic.task ?? 'unknown'}</span>
              {#if ds.panic.boots_since != null}
                <span class="strip-sep">·</span>
                <span class="panic-meta">{ds.panic.boots_since} boots ago</span>
              {/if}
            {:else}
              <span class="panic-meta">stored coredump only</span>
            {/if}
            <span class="spacer"></span>
            {#if ds.panic.coredump}
              <a class="btn outline sm" href={ds.coredumpUrl} download="coredump.bin">Download coredump</a>
            {/if}
            <button class="btn outline sm" onclick={ds.doClearPanic} disabled={ds.clearingPanic}>
              {ds.clearingPanic ? 'Clearing…' : 'Clear'}
            </button>
          </div>
          {#if ds.panic.available && ds.panic.panic_reason}
            <div class="panic-reason">{ds.panic.panic_reason.replace(/\s*\n\s*/g, ' ')}</div>
          {/if}
          {#if ds.clearPanicMsg && !ds.clearingPanic}<div class="hint panic-msg">{ds.clearPanicMsg}</div>{/if}
        </div>
      {/if}
    </div>
  </details>

  <details class="section disclosure">
    <summary>
      <h2>Tasks</h2>
      <span class="sum-meta">{ds.tasks.length} tasks</span>
    </summary>
    <div class="disclosure-body has-overlay-action">
      <button class="btn outline sm overlay-action" onclick={ds.loadTasks} disabled={ds.tasksLoading}>
        {ds.tasksLoading ? 'Loading…' : 'Refresh'}
      </button>
      {#if ds.tasksErr}<div class="status-msg" data-kind="err">{ds.tasksErr}</div>{/if}
      {#if ds.tasks.length === 0 && !ds.tasksLoading}
        <p class="muted">No task data.</p>
      {:else}
        <table class="drops">
          <thead>
            <tr>
              <th>Name</th><th>Prio</th><th>State</th><th>Stack HWM</th>
            </tr>
          </thead>
          <tbody>
            {#each ds.tasks as t}
              <tr>
                <td>{t.name}</td>
                <td>{t.prio}{t.prio !== t.base_prio ? ` (base ${t.base_prio})` : ''}</td>
                <td>{t.state}</td>
                <td class:warn={t.prio < 24 && t.stack_hwm < 512} class:danger={t.prio < 24 && t.stack_hwm < 256}>{t.stack_hwm}</td>
              </tr>
            {/each}
          </tbody>
        </table>
      {/if}
    </div>
  </details>

  <details class="section disclosure">
    <summary>
      <h2>Telemetry drops</h2>
      <span class="sum-meta">{ds.recentDrops.length} in window</span>
    </summary>
    <div class="disclosure-body">
      {#if ds.recentDrops.length === 0}
        <p class="muted">No recent drops.</p>
      {:else}
        <table class="drops">
          <thead>
            <tr>
              <th>Age</th><th>Chip</th><th>Kind</th><th>Addr</th><th>GH/s</th><th>Δ</th><th>Elapsed</th>
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
  </details>

</div>

<ConfirmDialog
  bind:open={ds.showRebootDialog}
  title="Reboot device?"
  message="Mining will be interrupted while the device restarts. It should return in about 15 seconds."
  confirmLabel="Reboot"
  danger
  skipKey={ds.REBOOT_SKIP_KEY}
  onconfirm={ds.doReboot}
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

  .health-row {
    display: flex;
    align-items: flex-start;
    gap: 24px;
    flex-wrap: wrap;
    margin-bottom: 12px;
  }
  .refresh-inline {
    margin-left: auto;
    align-self: center;
  }
  .has-overlay-action {
    position: relative;
  }
  .overlay-action {
    position: absolute;
    top: 0;
    right: 0;
    z-index: 1;
  }
  .health-cell {
    min-width: 180px;
  }
  .health-label {
    font-size: 11px;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    margin-bottom: 4px;
  }
  .health-value {
    display: inline-flex;
    align-items: center;
    gap: 10px;
    font-family: ui-monospace, monospace;
    font-size: 14px;
    color: var(--text);
  }
  .status[data-state="ok"] { color: var(--success); }
  .status[data-state="bad"] { color: var(--danger); }
  .status[data-state="unknown"] { color: var(--muted); }

  table.kv {
    width: 100%;
    border-collapse: collapse;
    font-size: 12px;
    font-family: ui-monospace, monospace;
    margin-top: 8px;
  }
  table.kv th, table.kv td {
    text-align: left;
    padding: 4px 8px;
    border-bottom: 1px solid var(--border, rgba(255, 255, 255, 0.08));
  }
  table.kv th {
    color: var(--text-dim);
    font-weight: 600;
  }
  table.kv td:not(:first-child) { text-align: right; }
  table.kv th:not(:first-child) { text-align: right; }

  td.warn { color: var(--warning); }
  td.danger { color: var(--danger); font-weight: 600; }

  .panic-block {
    margin-top: 14px;
  }
  .panic-head {
    display: flex;
    align-items: baseline;
    gap: 10px;
    flex-wrap: wrap;
  }
  .panic-head .health-label {
    margin-bottom: 0;
  }
  .panic-task {
    font-weight: 600;
    color: var(--danger);
    font-family: ui-monospace, monospace;
    font-size: 12px;
  }
  .panic-meta {
    font-size: 12px;
    color: var(--muted);
    font-family: ui-monospace, monospace;
  }
  .panic-reason {
    margin-top: 6px;
    font-size: 11px;
    color: var(--text-dim);
    font-family: ui-monospace, Menlo, monospace;
    white-space: nowrap;
    overflow-x: auto;
  }
  .panic-msg {
    margin-top: 6px;
  }

  .hint { font-size: 11px; color: var(--muted); }

  .btn.xs {
    padding: 2px 8px;
    font-size: 10px;
  }

  .status-strip {
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 16px;
    padding: 8px 0;
    border-bottom: 1px solid var(--border, rgba(255, 255, 255, 0.08));
    flex-wrap: wrap;
  }
  .strip-items {
    display: inline-flex;
    align-items: baseline;
    gap: 10px;
    flex-wrap: wrap;
    font-size: 12px;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }
  .strip-item strong {
    color: var(--text);
    font-weight: 600;
    font-family: ui-monospace, monospace;
    text-transform: none;
    letter-spacing: 0;
    margin-left: 4px;
  }
  .strip-sep {
    color: var(--text-dim);
    opacity: 0.5;
  }
  .strip-item[data-state="ok"] strong { color: var(--success); }
  .strip-item[data-state="err"] strong { color: var(--danger); }
  .strip-actions {
    display: inline-flex;
    align-items: center;
    gap: 8px;
  }

  /* details.disclosure marker + summary styling lives in ui-kit/utilities.css */
  .disclosure-body {
    margin-top: 14px;
  }
</style>
