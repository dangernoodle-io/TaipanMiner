<script lang="ts">
  import { onMount, onDestroy } from 'svelte'
  import { route } from '../lib/router'
  import { info } from '../lib/stores'
  import { fetchKnot, type KnotPeer } from '../lib/api'

  let peers: KnotPeer[] = []
  let loading = true
  let loadErr = ''
  let lastFetch = 0

  async function load() {
    try {
      peers = await fetchKnot()
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
  })

  onDestroy(() => {
    document.removeEventListener('visibilitychange', handleVisibilityChange)
  })

  function getStateBadgeClass(state: string): string {
    switch (state) {
      case 'mining':
        return 'state-mining'
      case 'ota':
        return 'state-ota'
      case 'provisioning':
        return 'state-provisioning'
      case 'idle':
        return 'state-idle'
      default:
        return 'state-neutral'
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
</script>

<div class="knot-container">
  <div class="header">
    <h1>Knot</h1>
    <button on:click={handleRefresh} disabled={loading}>
      {loading ? 'Loading...' : 'Refresh'}
    </button>
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
            <th>State</th>
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
                <span class={`badge ${getStateBadgeClass(peer.state)}`}>
                  {peer.state}
                </span>
              </td>
            </tr>
          {/each}
        </tbody>
      </table>
    </div>

    {#if legendBoards.length > 0}
      <div class="legend">
        {#each legendBoards as b}
          <span class="legend-item">
            <span class={`board-dot ${boardClass(b)}`} title={b}></span>
            {b}
          </span>
        {/each}
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

  .header button {
    padding: 8px 16px;
    background: var(--accent);
    color: white;
    border: none;
    border-radius: 4px;
    cursor: pointer;
    font-size: 14px;
    font-weight: 500;
    transition: opacity 0.2s;
  }

  .header button:hover:not(:disabled) {
    opacity: 0.9;
  }

  .header button:disabled {
    opacity: 0.5;
    cursor: not-allowed;
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

  tbody tr.current-device {
    background: rgba(0, 200, 100, 0.1);
    border-left: 3px solid var(--accent);
    padding-left: 9px;
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

  .state-mining {
    background: rgba(0, 200, 100, 0.2);
    color: #00c864;
  }

  .state-ota {
    background: rgba(255, 200, 0, 0.2);
    color: #ffc800;
  }

  .state-provisioning {
    background: rgba(0, 150, 255, 0.2);
    color: #0096ff;
  }

  .state-idle {
    background: rgba(150, 150, 150, 0.2);
    color: #aaaaaa;
  }

  .state-neutral {
    background: rgba(100, 100, 100, 0.2);
    color: #999999;
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
    flex-wrap: wrap;
    justify-content: center;
    gap: 16px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 16px;
    font-size: 12px;
    color: var(--label);
    text-align: center;
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
