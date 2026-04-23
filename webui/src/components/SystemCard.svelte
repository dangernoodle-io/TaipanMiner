<script lang="ts">
  import { stats, info } from '../lib/stores'

  function fmtBytes(b: number | null | undefined): string {
    if (b == null) return '—'
    if (b < 1024) return `${b} B`
    if (b < 1024 * 1024) return `${(b / 1024).toFixed(0)} KB`
    return `${(b / 1024 / 1024).toFixed(1)} MB`
  }

  function rssiBars(r: number | null | undefined): string {
    if (r == null) return ''
    if (r >= -55) return '▮▮▮▮'
    if (r >= -65) return '▮▮▮▯'
    if (r >= -75) return '▮▮▯▯'
    if (r >= -85) return '▮▯▯▯'
    return '▯▯▯▯'
  }

  function fmtDate(ts: number | null | undefined): string {
    if (ts == null || ts <= 0) return '—'
    try {
      return new Date(ts * 1000).toLocaleString()
    } catch {
      return '—'
    }
  }
</script>

<section class="card">
  <h3>System</h3>
  <dl>
    <div><dt>Board</dt><dd>{$stats?.board ?? '—'}</dd></div>
    <div><dt>Version</dt><dd>{$stats?.version ?? '—'}</dd></div>
    <div><dt>Built</dt><dd>{$stats ? `${$stats.build_date} ${$stats.build_time}` : '—'}</dd></div>
    <div><dt>IDF</dt><dd>{$info?.idf_version ?? '—'}</dd></div>
    <div><dt>Cores</dt><dd>{$info?.cores ?? '—'}</dd></div>
    <div><dt>MAC</dt><dd class="mono">{$info?.mac ?? '—'}</dd></div>
    <div><dt>SSID</dt><dd>{$info?.ssid ?? '—'}</dd></div>
    <div>
      <dt>RSSI</dt>
      <dd>
        {$stats?.rssi_dbm ?? '—'}{#if $stats?.rssi_dbm != null}<span class="dim"> dBm</span> <span class="bars">{rssiBars($stats.rssi_dbm)}</span>{/if}
      </dd>
    </div>
    <div><dt>Heap</dt><dd>{fmtBytes($stats?.free_heap)} / {fmtBytes($stats?.total_heap)}</dd></div>
    <div><dt>Flash</dt><dd>{fmtBytes($info?.app_size)} / {fmtBytes($info?.flash_size)}</dd></div>
    <div><dt>Reset</dt><dd>{$info?.reset_reason ?? '—'}</dd></div>
    <div><dt>WDT resets</dt><dd>{$info?.wdt_resets ?? '—'}</dd></div>
    <div><dt>Last boot</dt><dd>{fmtDate($info?.boot_time)}</dd></div>
    <div><dt>Wallet</dt><dd class="mono trunc" title={$stats?.wallet}>{$stats?.wallet ?? '—'}</dd></div>
  </dl>
</section>

<style>
  .card {
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 16px;
  }

  h3 {
    margin: 0 0 12px 0;
    font-size: 12px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    font-weight: 600;
  }

  dl {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: 6px 18px;
    margin: 0;
  }

  dl > div {
    display: grid;
    grid-template-columns: 1fr auto;
    gap: 12px;
    align-items: baseline;
    min-width: 0;
    font-size: 12px;
    border-bottom: 1px dotted var(--border);
    padding: 3px 0;
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
    font-variant-numeric: tabular-nums;
    text-align: right;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .mono {
    font-family: ui-monospace, Menlo, monospace;
    font-size: 11px;
  }

  .trunc {
    max-width: 180px;
  }

  .dim {
    color: var(--muted);
  }

  .bars {
    color: var(--accent);
    margin-left: 3px;
  }
</style>
