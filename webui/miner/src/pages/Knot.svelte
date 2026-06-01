<script lang="ts">
  import { onMount, onDestroy } from 'svelte'
  import { route } from '../lib/router'
  import { info } from '../lib/stores'
  import { fetchKnot, type KnotPeer } from '../lib/api'
  import { fmtRelative, fmtHashGhs } from '../lib/fmt'

  let peers: KnotPeer[] = []
  let loading = true
  let loadErr = ''
  let lastFetch = 0
  let now = Date.now()
  let nowTimer: ReturnType<typeof setInterval> | null = null
  let knotTimer: ReturnType<typeof setInterval> | null = null
  let statsTimer: ReturnType<typeof setInterval> | null = null
  const KNOT_INTERVAL_MS = 60000   // mDNS peer list — membership changes rarely
  const STATS_INTERVAL_MS = 8000   // per-peer live stats — refreshed independently

  // Per-peer live stats, fetched directly from each peer's own API (the local
  // device must not fan out N blocking requests). Keyed by peer.instance.
  type PeerStat = {
    loading: boolean
    reachable: boolean
    ghs: number | null
    shares: number | null
  }
  let peerStats: Record<string, PeerStat> = {}

  $: lastFetchAgoS = lastFetch > 0 ? Math.floor((now - lastFetch) / 1000) : null
  /* The peer list refreshes every 60s; >150s without a successful refresh means
   * polling stalled (network drop, tab throttled, fetch erroring). Flag it. */
  $: stale = lastFetchAgoS != null && lastFetchAgoS > 150

  // Other miners only — never list the device that's serving this page.
  $: displayPeers = peers.filter((p) => p.hostname !== $info?.hostname)
  $: legendBoards = Array.from(new Set(displayPeers.map((p) => p.board).filter(Boolean))).sort()
  $: legendStatuses = Array.from(new Set(displayPeers.map((p) => p.state || 'unknown'))).sort()

  async function fetchPeerStats(peer: KnotPeer) {
    // Only show the loading placeholder on the first fetch. On later polls keep
    // the last values on screen and swap them in when the new data lands —
    // otherwise every 30s mDNS refresh flashes the whole list to "…".
    if (!peerStats[peer.instance]) {
      peerStats = { ...peerStats, [peer.instance]: { loading: true, reachable: false, ghs: null, shares: null } }
    }
    let ghs: number | null = null, shares: number | null = null
    let reachable = false
    try {
      const res = await fetch(`http://${peer.ip}/api/stats`, { signal: AbortSignal.timeout(4000) })
      if (res.ok) {
        reachable = true
        const s = await res.json()
        ghs = s.asic_total_ghs ?? (s.hashrate ? s.hashrate / 1e9 : null)
        shares = s.session_shares ?? null
      }
    } catch {
      /* unreachable / timed out — leave reachable=false */
    }
    peerStats = { ...peerStats, [peer.instance]: { loading: false, reachable, ghs, shares } }
  }

  // Fan out live-stat fetches across the current peer set (browser-side).
  function refreshStats() {
    peers.forEach(fetchPeerStats)
  }

  async function load() {
    try {
      peers = (await fetchKnot()).sort((a, b) => a.hostname.localeCompare(b.hostname))
      loadErr = ''
      lastFetch = Date.now()
      refreshStats()  // pull stats for the (possibly changed) peer set immediately
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
    /* Skip while hidden — visibilitychange handler fires a refresh on return. */
    knotTimer = setInterval(() => {
      if (document.visibilityState === 'visible' && $route === 'knot') load()
    }, KNOT_INTERVAL_MS)
    statsTimer = setInterval(() => {
      if (document.visibilityState === 'visible' && $route === 'knot') refreshStats()
    }, STATS_INTERVAL_MS)
  })

  onDestroy(() => {
    document.removeEventListener('visibilitychange', handleVisibilityChange)
    if (nowTimer !== null) clearInterval(nowTimer)
    if (knotTimer !== null) clearInterval(knotTimer)
    if (statsTimer !== null) clearInterval(statsTimer)
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

  /* IDF's mDNS hostname is bare (e.g. "tdongles3-3"); the browser needs
   * the .local suffix to resolve. Append if it isn't already there. */
  function fqdn(hostname: string): string {
    if (!hostname) return ''
    return hostname.endsWith('.local') ? hostname : `${hostname}.local`
  }

  /* Display the bare hostname (no .local) so it's less noisy. */
  function displayHost(hostname: string): string {
    if (!hostname) return ''
    return hostname.endsWith('.local') ? hostname.slice(0, -'.local'.length) : hostname
  }

  /* Board → dot color, derived entirely from the name so any board (current or
   * future) gets a stable, distinct color with no code change. */
  function boardColor(board: string): string {
    if (!board) return '#888888'
    let h = 0
    for (let i = 0; i < board.length; i++) h = (h * 31 + board.charCodeAt(i)) >>> 0
    return `hsl(${h % 360}, 60%, 58%)`
  }
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
      <button class="btn outline sm" onclick={handleRefresh} disabled={loading}>
        {loading ? 'Loading...' : 'Refresh'}
      </button>
    </div>
  </div>

  {#if loadErr}
    <div class="error">{loadErr}</div>
  {/if}

  {#if displayPeers.length === 0 && !loading}
    <div class="empty-state">
      No other miners discovered yet — rows fill in as devices announce.
    </div>
  {/if}

  {#if displayPeers.length > 0}
    <div class="cards">
      {#each displayPeers as peer (peer.instance)}
        {@const st = peerStats[peer.instance]}
        <div class="card">
          <div class="identity">
            <div class="name-row">
              <span class="board-dot" style={`background: ${boardColor(peer.board)}`} title={peer.board}></span>
              <a class="name" href={`http://${fqdn(peer.hostname)}`} target="_blank" rel="noopener">
                {displayHost(peer.hostname)}
              </a>
              <span class="ip">{peer.ip}</span>
              <span
                class={`status-dot ${getStateBadgeClass(peer.state || 'unknown')}`}
                class:pulse={peer.state === 'mining'}
                title={peer.state || 'unknown'}
              ></span>
            </div>
            <div class="sub">
              <span class="board">{peer.board || '—'}</span>
              <span class="ver" title="firmware">{peer.version || '—'}</span>
            </div>
          </div>

          <div class="stats">
            {#if !st || st.loading}
              <span class="muted">…</span>
            {:else if st.reachable}
              <div class="stat">
                <span class="sv">{st.shares != null ? st.shares : '—'}</span>
                <span class="sl">shares</span>
              </div>
              <div class="stat">
                <span class="sv hashrate">{st.ghs != null ? fmtHashGhs(st.ghs) : '—'}</span>
                <span class="sl">hashrate</span>
              </div>
            {:else}
              <span class="muted unreachable">unreachable</span>
            {/if}
          </div>
        </div>
      {/each}
    </div>

    {#if legendBoards.length > 0 || legendStatuses.length > 0}
      <div class="legend">
        {#if legendBoards.length > 0}
          <div class="legend-row">
            <span class="legend-label">Boards</span>
            {#each legendBoards as b}
              <span class="legend-item">
                <span class="board-dot" style={`background: ${boardColor(b)}`} title={b}></span>
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

  /* One full-width row per miner. */
  .cards {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .card {
    display: flex;
    align-items: center;
    gap: 16px;
    padding: 12px 16px;
    background: var(--surface);
    border: 1px solid var(--border);
    border-radius: 8px;
  }

  .identity {
    display: flex;
    flex-direction: column;
    gap: 3px;
    min-width: 0;
    width: 260px;
    flex-shrink: 0;
  }
  .name-row {
    display: flex;
    align-items: baseline;
    gap: 8px;
  }
  /* Board dot leads the hostname, so keep it centered on the name; the mining
   * dot trails the IP and baselines with the text like everything else. */
  .name-row .board-dot {
    align-self: center;
  }
  .name-row .name {
    font-size: 15px;
    font-weight: 600;
    color: var(--accent);
    text-decoration: none;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }
  .name-row .name:hover { opacity: 0.8; }

  /* IP beside the hostname — keep the small muted look it had in the sub line. */
  .name-row .ip {
    font-size: 11px;
    color: var(--label);
    font-variant-numeric: tabular-nums;
    white-space: nowrap;
  }

  .sub {
    display: flex;
    gap: 10px;
    font-size: 11px;
    color: var(--label);
    overflow: hidden;
  }
  .sub .ver {
    font-variant-numeric: tabular-nums;
  }
  .sub .ver {
    color: var(--muted);
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .stats {
    margin-left: auto;
    display: flex;
    align-items: center;
    gap: 24px;
  }
  .stat {
    display: flex;
    flex-direction: column;
    gap: 1px;
    width: 96px;
    text-align: right;
  }
  .stat .sv {
    font-size: 15px;
    font-weight: 600;
    color: var(--text);
    font-variant-numeric: tabular-nums;
    line-height: 1.1;
    white-space: nowrap;
  }
  /* match the Hero hashrate color */
  .stat .sv.hashrate {
    color: var(--accent);
  }
  .stat .sl {
    font-size: 9px;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--label);
  }
  .stats .muted { font-size: 12px; color: var(--muted); }
  .stats .unreachable { font-style: italic; }

  .board-dot {
    display: inline-block;
    width: 8px;
    height: 8px;
    border-radius: 50%;
    background: #888;
    flex-shrink: 0;
  }

  .status-dot {
    display: inline-block;
    width: 6px;
    height: 6px;
    border-radius: 50%;
    background: currentColor;
    flex-shrink: 0;
  }
  .state-mining       { color: #00c864; }
  .state-ota          { color: #ffc800; }
  .state-provisioning { color: #0096ff; }
  .state-idle         { color: #aaaaaa; }
  .state-neutral      { color: #999999; }
  .state-unknown      { color: #d8965a; }

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

  @media (max-width: 560px) {
    .identity { width: auto; flex: 1; }
    .stats { gap: 14px; }
    .stat { width: 60px; }
  }
</style>
