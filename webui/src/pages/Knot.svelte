<script lang="ts">
  import { onMount, onDestroy } from 'svelte'
  import { route } from '../lib/router'
  import { info } from '../lib/stores'
  import { fetchKnot, type KnotPeer } from '../lib/api'
  import { fmtRelative } from '../lib/fmt'

  let peers: KnotPeer[] = []
  let loading = true
  let loadErr = ''
  let lastFetch = 0
  let now = Date.now()
  let nowTimer: ReturnType<typeof setInterval> | null = null
  let pollTimer: ReturnType<typeof setInterval> | null = null
  const POLL_INTERVAL_MS = 30000

  $: lastFetchAgoS = lastFetch > 0 ? Math.floor((now - lastFetch) / 1000) : null
  /* Poll runs every 30s; >75s without a successful refresh means polling
   * stalled (network drop, tab throttled, fetch erroring). Flag the indicator. */
  $: stale = lastFetchAgoS != null && lastFetchAgoS > 75

  async function load() {
    try {
      peers = (await fetchKnot()).sort((a, b) => a.hostname.localeCompare(b.hostname))
      loadErr = ''
      lastFetch = Date.now()
    } catch (e) {
      loadErr = (e as Error).message
    } finally {
      loading = false
    }
  }

  function handleRefresh() {
    loading = true
    load()
  }

  function handleVisibilityChange() {
    if (document.visibilityState === 'visible' && $route === 'knot') {
      load()
    }
  }

  onMount(() => {
    load()
    document.addEventListener('visibilitychange', handleVisibilityChange)
    nowTimer = setInterval(() => { now = Date.now() }, 1000)
    pollTimer = setInterval(() => {
      /* Skip while hidden — visibilitychange handler fires a refresh on return. */
      if (document.visibilityState === 'visible' && $route === 'knot') load()
    }, POLL_INTERVAL_MS)
  })

  onDestroy(() => {
    document.removeEventListener('visibilitychange', handleVisibilityChange)
    if (nowTimer !== null) clearInterval(nowTimer)
    if (pollTimer !== null) clearInterval(pollTimer)
  })

  function getStateBadgeClass(state: string): string {
    switch (state) {
      case 'mining':       return 'state-mining'
      case 'ota':          return 'state-ota'
      case 'provisioning': return 'state-provisioning'
      case 'idle':         return 'state-idle'
      case 'unknown':      return 'state-unknown'
      default:             return 'state-neutral'
    }
  }

  function isCurrentDevice(hostname: string): boolean {
    return hostname === $info?.hostname
  }

  /* IDF's mDNS hostname is bare (e.g. "tdongles3-3"); the browser needs
   * the .local suffix to resolve. Append if it isn't already there. */
  function fqdn(hostname: string): string {
    if (!hostname) return ''
    return hostname.endsWith('.local') ? hostname : `${hostname}.local`
  }

  /* Display the bare hostname (no .local) so the column is less noisy.
   * The href still uses fqdn() so navigation works. */
  function displayHost(hostname: string): string {
    if (!hostname) return ''
    return hostname.endsWith('.local')
      ? hostname.slice(0, -'.local'.length)
      : hostname
  }

  /* Board → short class name for the colored dot. Unknown boards fall
   * through to a neutral class. Add new boards here as they ship. */
  function boardClass(board: string): string {
    switch (board) {
      case 'tdongle-s3':  return 'board-tdongle-s3'
      case 'bitaxe-601':  return 'board-bitaxe-601'
      case 'bitaxe-403':  return 'board-bitaxe-403'
      case 'bitaxe-650':  return 'board-bitaxe-650'
      default:            return 'board-other'
    }
  }

  /* Distinct boards present in the current peer set, for the legend. */
  $: legendBoards = Array.from(new Set(peers.map(p => p.board).filter(Boolean))).sort()

  /* Distinct statuses present in the peer set, for the status legend. Empty
   * states surface as "unknown" so the user knows they're not reporting yet. */
  $: legendStatuses = Array.from(
    new Set(peers.map(p => p.state || 'unknown'))
  ).sort()
</script>

<div class="knot-container">
  <div class="header">
    <h1>Knot</h1>
    <div class="actions">
      {#if lastFetchAgoS != null}
        <span class="updated" class:stale title="Time since the last successful /api/knot fetch">
          Updated {fmtRelative(lastFetchAgoS)}
        </span>
      {/if}
      <button class="btn outline sm" on:click={handleRefresh} disabled={loading}>
        {loading ? 'Loading...' : 'Refresh'}
      </button>
    </div>
  </div>

  {#if loadErr}
    <div class="error">{loadErr}</div>
  {/if}

  {#if peers.length === 0 && !loading}
    <div class="empty-state">
      No miners discovered yet — table fills as devices announce.
    </div>
  {/if}

  {#if peers.length > 0}
    <div class="table-wrapper">
      <table>
        <thead>
          <tr>
            <th>Hostname</th>
            <th>Version</th>
            <th>Status</th>
          </tr>
        </thead>
        <tbody>
          {#each peers as peer (peer.instance)}
            <tr class:current-device={isCurrentDevice(peer.hostname)}>
              <td>
                <span class={`board-dot ${boardClass(peer.board)}`} title={peer.board}></span>
                <a href={`http://${fqdn(peer.hostname)}`} target="_blank" rel="noopener">
                  {displayHost(peer.hostname)}
                </a>
              </td>
              <td>{peer.version}</td>
              <td>
                <span
                  class={`status-dot ${getStateBadgeClass(peer.state || 'unknown')}`}
                  title={peer.state || 'unknown'}
                ></span>
              </td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>

    {#if legendBoards.length > 0 || legendStatuses.length > 0}
      <div class="legend">
        {#if legendBoards.length > 0}
          <div class="legend-row">
            <span class="legend-label">Boards</span>
            {#each legendBoards as b}
              <span class="legend-item">
                <span class={`board-dot ${boardClass(b)}`} title={b}></span>
                {b}
              </span>
            {/each}
          </div>
        {/if}
        {#if legendStatuses.length > 0}
          <div class="legend-row">
            <span class="legend-label">Status</span>
            {#each legendStatuses as s}
              <span class="legend-item">
                <span class={`status-dot ${getStateBadgeClass(s)}`} title={s}></span>
                {s}
              </span>
            {/each}
          </div>
        {/if}
      </div>
    {/if}
  {/if}
</div>

<style>
  .knot-container {
    display: flex;
    flex-direction: column;
    gap: 16px;
  }

  .header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 8px;
  }

  .header h1 {
    margin: 0;
    font-size: 24px;
    font-weight: 600;
  }

  .actions {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  .updated {
    font-size: 11px;
    color: var(--label);
  }

  .updated.stale {
    color: var(--warning, #f39c12);
  }


  .error {
    padding: 12px;
    background: #3d2020;
    color: #ff6b6b;
    border-radius: 4px;
    font-size: 13px;
  }

  .empty-state {
    padding: 32px;
    text-align: center;
    color: var(--muted);
    font-size: 14px;
  }

  .table-wrapper {
    overflow-x: auto;
  }

  table {
    width: 100%;
    border-collapse: collapse;
    font-size: 13px;
  }

  thead {
    background: var(--surface);
    border-bottom: 1px solid var(--border);
  }

  th {
    text-align: left;
    padding: 12px;
    font-weight: 500;
    color: var(--label);
  }

  td {
    padding: 12px;
    border-bottom: 1px solid var(--border);
    color: var(--text);
  }

  tbody tr {
    transition: background 0.15s;
  }

  tbody tr:hover {
    background: rgba(255, 255, 255, 0.03);
  }

  tbody tr.current-device td {
    background: rgba(0, 200, 100, 0.08);
  }
  tbody tr.current-device td:first-child {
    box-shadow: inset 3px 0 0 var(--accent);
  }

  .badge {
    display: inline-block;
    padding: 4px 8px;
    border-radius: 3px;
    font-size: 11px;
    font-weight: 500;
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  .state-mining       { background: rgba(0, 200, 100, 0.2); color: #00c864; }
  .state-ota          { background: rgba(255, 200, 0, 0.2);  color: #ffc800; }
  .state-provisioning { background: rgba(0, 150, 255, 0.2);  color: #0096ff; }
  .state-idle         { background: rgba(150, 150, 150, 0.2); color: #aaaaaa; }
  .state-neutral      { background: rgba(100, 100, 100, 0.2); color: #999999; }
  .state-unknown      { background: rgba(180, 120, 60, 0.18); color: #d8965a; }

  /* Solid-fill dot in the table cell — paint the dot with the status color
   * (`color`) rather than its translucent background. */
  .status-dot {
    display: inline-block;
    width: 10px;
    height: 10px;
    border-radius: 50%;
    background: currentColor;
    vertical-align: middle;
  }


  a {
    color: var(--accent);
    text-decoration: none;
    transition: opacity 0.2s;
  }

  a:hover {
    opacity: 0.8;
  }

  .legend {
    display: flex;
    flex-direction: column;
    gap: 10px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 16px;
    font-size: 12px;
    color: var(--label);
  }

  .legend-row {
    display: flex;
    flex-wrap: wrap;
    align-items: center;
    justify-content: center;
    gap: 12px;
  }

  .legend-label {
    font-size: 10px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
    margin-right: 4px;
    min-width: 50px;
    text-align: right;
  }

  .legend-item {
    display: inline-flex;
    align-items: center;
    gap: 6px;
  }

  .board-dot {
    display: inline-block;
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: #888;
    vertical-align: middle;
    margin-right: 6px;
    flex-shrink: 0;
  }

  .legend-item .board-dot {
    margin-right: 0;
  }

  /* Per-board colors. Pick distinct hues for each board family. */
  .board-tdongle-s3 { background: #3b82f6; }  /* blue */
  .board-bitaxe-601 { background: #10b981; }  /* green */
  .board-bitaxe-403 { background: #f59e0b; }  /* amber */
  .board-bitaxe-650 { background: #a855f7; }  /* purple */
  .board-other      { background: #888888; }  /* neutral fallback */
</style>
