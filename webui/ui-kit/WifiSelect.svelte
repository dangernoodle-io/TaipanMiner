<script lang="ts">
  type WifiNetwork = { ssid: string; rssi: number; secure: boolean }

  const MANUAL_VALUE = '__manual__'

  let {
    networks,
    selected = $bindable(),
    scanning = false,
    error = null,
    disabled = false,
    allowManualEntry = true,
    manualLabel = 'Manual entry…',
    scanningLabel = 'Scanning…',
    emptyLabel = 'No networks found',
    manualSelectedLabel = 'Manual entry'
  }: {
    networks: WifiNetwork[]
    selected: string
    scanning?: boolean
    error?: string | null
    disabled?: boolean
    allowManualEntry?: boolean
    manualLabel?: string
    scanningLabel?: string
    emptyLabel?: string
    manualSelectedLabel?: string
  } = $props()

  let open = $state(false)
  let containerEl: HTMLDivElement | undefined

  function selectedNetwork(): WifiNetwork | undefined {
    return networks.find(n => n.ssid === selected)
  }

  function pick(value: string) {
    selected = value
    open = false
  }

  function toggle() {
    if (!disabled && !scanning) open = !open
  }

  $effect(() => {
    if (!open) return
    function onClick(e: MouseEvent) {
      if (containerEl && !containerEl.contains(e.target as Node)) {
        open = false
      }
    }
    document.addEventListener('click', onClick)
    return () => document.removeEventListener('click', onClick)
  })
</script>

<div class="wifi-select" bind:this={containerEl}>
  <button
    type="button"
    class="trigger"
    onclick={toggle}
    disabled={disabled || scanning}
    aria-haspopup="listbox"
    aria-expanded={open}
  >
    {#if scanning}
      <span class="text muted">{scanningLabel}</span>
    {:else if selected === MANUAL_VALUE}
      <span class="text">{manualSelectedLabel}</span>
    {:else if selectedNetwork()}
      {@const ap = selectedNetwork()!}
      <span class="ssid">{ap.ssid}</span>
      <span class="meta">
        {#if ap.secure}
          <svg width="16" height="16" viewBox="0 0 16 16" aria-hidden="true">
            <rect x="2" y="8" width="12" height="7" rx="1" fill="currentColor"/>
            <path d="M5 8V5a3 3 0 016 0v3" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
          </svg>
        {/if}
        <svg width="16" height="16" viewBox="0 0 24 24" aria-hidden="true">
          <path d="M9.1 16.8A4.5 4.5 0 0114.9 16.8L13 18.8A1.5 1.5 0 0011 18.8Z" fill={ap.rssi >= -80 ? 'currentColor' : 'var(--muted)'}/>
          <path d="M5.6 12.4A10 10 0 0118.4 12.4L16.4 14.6A7 7 0 007.6 14.6Z" fill={ap.rssi >= -67 ? 'currentColor' : 'var(--muted)'}/>
          <path d="M2.2 7.8A15.5 15.5 0 0121.8 7.8L19.8 10A12 12 0 004.2 10Z" fill={ap.rssi >= -50 ? 'currentColor' : 'var(--muted)'}/>
        </svg>
      </span>
    {:else if error}
      <span class="text muted">{error}</span>
    {:else}
      <span class="text muted">{emptyLabel}</span>
    {/if}
  </button>
  {#if open}
    <ul class="list" role="listbox">
      {#each networks as ap (ap.ssid)}
        <li>
          <button
            type="button"
            class="item"
            onclick={() => pick(ap.ssid)}
            role="option"
            aria-selected={selected === ap.ssid}
          >
            <span class="ssid">{ap.ssid}</span>
            <span class="meta">
              {#if ap.secure}
                <svg width="16" height="16" viewBox="0 0 16 16" aria-hidden="true">
                  <rect x="2" y="8" width="12" height="7" rx="1" fill="currentColor"/>
                  <path d="M5 8V5a3 3 0 016 0v3" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"/>
                </svg>
              {/if}
              <svg width="16" height="16" viewBox="0 0 24 24" aria-hidden="true">
                <path d="M9.1 16.8A4.5 4.5 0 0114.9 16.8L13 18.8A1.5 1.5 0 0011 18.8Z" fill={ap.rssi >= -80 ? 'currentColor' : 'var(--muted)'}/>
                <path d="M5.6 12.4A10 10 0 0118.4 12.4L16.4 14.6A7 7 0 007.6 14.6Z" fill={ap.rssi >= -67 ? 'currentColor' : 'var(--muted)'}/>
                <path d="M2.2 7.8A15.5 15.5 0 0121.8 7.8L19.8 10A12 12 0 004.2 10Z" fill={ap.rssi >= -50 ? 'currentColor' : 'var(--muted)'}/>
              </svg>
            </span>
          </button>
        </li>
      {/each}
      {#if allowManualEntry}
        <li>
          <button
            type="button"
            class="item manual"
            onclick={() => pick(MANUAL_VALUE)}
            role="option"
            aria-selected={selected === MANUAL_VALUE}
          >
            <span class="ssid muted">{manualLabel}</span>
          </button>
        </li>
      {/if}
    </ul>
  {/if}
</div>

<style>
  .wifi-select {
    position: relative;
    flex: 1;
  }

  .trigger {
    width: 100%;
    padding: 12px;
    padding-right: 30px;
    background: var(--input);
    color: var(--text);
    border: 1px solid var(--border);
    border-radius: 4px;
    font-size: 14px;
    font-family: inherit;
    cursor: pointer;
    text-align: left;
    min-height: 44px;
    display: flex;
    justify-content: space-between;
    align-items: center;
    position: relative;
  }

  .trigger:disabled {
    opacity: 0.6;
    cursor: not-allowed;
  }

  .trigger::after {
    content: '';
    position: absolute;
    right: 12px;
    top: 50%;
    transform: translateY(-50%);
    border-left: 5px solid transparent;
    border-right: 5px solid transparent;
    border-top: 5px solid var(--accent);
  }

  .ssid {
    flex: 1;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .text {
    flex: 1;
  }

  .text.muted,
  .ssid.muted {
    color: var(--label);
  }

  .meta {
    display: flex;
    align-items: center;
    gap: 6px;
    flex-shrink: 0;
    margin-left: 10px;
    color: var(--accent);
  }

  .meta :global(svg) {
    vertical-align: middle;
  }

  .list {
    position: absolute;
    top: 100%;
    left: 0;
    right: 0;
    background: var(--input);
    border: 1px solid var(--border);
    border-top: none;
    border-radius: 0 0 4px 4px;
    max-height: 200px;
    overflow-y: auto;
    z-index: 10;
    margin: 0;
    padding: 0;
    list-style: none;
  }

  .list li {
    margin: 0;
  }

  .item {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 10px 30px 10px 12px;
    cursor: pointer;
    border-bottom: 1px solid var(--border);
    background: none;
    border-left: none;
    border-right: none;
    border-top: none;
    color: var(--text);
    font-family: inherit;
    font-size: 14px;
    width: 100%;
    text-align: left;
  }

  .list li:last-child .item {
    border-bottom: none;
  }

  .item:hover {
    background: var(--border);
  }
</style>
