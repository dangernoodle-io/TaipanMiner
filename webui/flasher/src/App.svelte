<script lang="ts">
  import Brand from 'ui-kit/Brand.svelte'
  import Select from 'ui-kit/Select.svelte'
  import boards from './generated/boards.json'
  import { loadManifest, loadAsset, type Manifest } from './lib/release'
  import { ESPLoader, Transport } from 'esptool-js'

  type ConnectStatus = 'idle' | 'connecting' | 'connected' | 'error'
  type FlashStatus = 'idle' | 'downloading' | 'flashing' | 'done' | 'error'

  const webSerialAvailable = typeof navigator !== 'undefined' && 'serial' in navigator

  let board = $state('')
  let connectStatus = $state<ConnectStatus>('idle')
  let connectError = $state<string | null>(null)
  let chipInfo = $state<{ chip: string; mac: string; flashSize: string } | null>(null)
  let esploader = $state<ESPLoader | null>(null)
  let transport = $state<Transport | null>(null)
  // Web Serial types are not in lib.dom for older TS targets; use unknown.
  let activePort = $state<unknown>(null)
  let deviceDisconnected = $state(false)

  $effect(() => {
    const releasePort = () => {
      transport?.disconnect().catch(() => {})
    }
    const onDisconnect = (e: Event) => {
      const target = (e as unknown as { target: unknown }).target
      if (!activePort || target !== activePort) return
      deviceDisconnected = true
      transport = null
      esploader = null
      chipInfo = null
      activePort = null
      connectStatus = 'idle'
      if (flashStatus === 'flashing' || flashStatus === 'downloading') {
        flashStatus = 'error'
        flashError = 'Device disconnected mid-operation'
      }
    }
    window.addEventListener('beforeunload', releasePort)
    window.addEventListener('pagehide', releasePort)
    const serial = (navigator as unknown as { serial?: EventTarget }).serial
    if (serial) {
      serial.addEventListener('disconnect', onDisconnect)
    }
    return () => {
      window.removeEventListener('beforeunload', releasePort)
      window.removeEventListener('pagehide', releasePort)
      if (serial) {
        serial.removeEventListener('disconnect', onDisconnect)
      }
    }
  })

  async function disconnect() {
    try {
      await transport?.disconnect()
    } catch {
      // ignore
    }
    transport = null
    esploader = null
    chipInfo = null
    activePort = null
    connectStatus = 'idle'
  }
  let flashStatus = $state<FlashStatus>('idle')
  let flashError = $state<string | null>(null)
  let manifest = $state<Manifest | null>(null)
  let manifestError = $state<string | null>(null)
  let downloadedBin = $state<Uint8Array | null>(null)
  let downloadProgress = $state<{ loaded: number; total: number } | null>(null)
  let flashProgress = $state<{ written: number; total: number } | null>(null)

  const boardOptions = boards.map(b => ({
    value: b.id,
    label: `${b.label}${b.asic ? ` (${b.asic})` : ''}`
  }))

  $effect(() => {
    loadManifest()
      .then(m => {
        manifest = m
        manifestError = null
      })
      .catch(e => {
        manifestError = e instanceof Error ? e.message : String(e)
      })
  })

  const CONNECT_TIMEOUT_MS = 30_000

  function withTimeout<T>(p: Promise<T>, ms: number, label: string): Promise<T> {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(
        () => reject(new Error(`${label} timed out after ${ms / 1000}s — try unplugging and replugging the device`)),
        ms,
      )
      p.then(v => { clearTimeout(timer); resolve(v) }, e => { clearTimeout(timer); reject(e) })
    })
  }

  async function connect() {
    connectError = null
    deviceDisconnected = false
    connectStatus = 'connecting'
    try {
      // @ts-expect-error — Web Serial types may not be in lib.dom for older TS targets
      const port = await navigator.serial.requestPort({})
      activePort = port
      const t = new Transport(port, true)
      transport = t
      const loader = new ESPLoader({
        transport: t,
        baudrate: 115200,
        terminal: {
          clean: () => {},
          writeLine: (line: string) => console.log('[esptool]', line),
          write: (chunk: string) => console.log('[esptool]', chunk)
        }
      })
      const chip = await withTimeout(loader.main(), CONNECT_TIMEOUT_MS, 'Chip sync')
      const mac = await withTimeout(loader.chip.readMac(loader), 5_000, 'MAC read')
      let flashSize = 'unknown'
      try {
        flashSize = await withTimeout(loader.detectFlashSize(), 5_000, 'Flash detect')
      } catch {
        // best-effort
      }
      chipInfo = { chip, mac, flashSize }
      esploader = loader
      connectStatus = 'connected'
    } catch (e) {
      try { await transport?.disconnect() } catch { /* ignore */ }
      transport = null
      activePort = null
      const raw = e instanceof Error ? e.message : String(e)
      if (/No port selected by the user/i.test(raw)) {
        connectStatus = 'idle'
        connectError = null
        return
      }
      connectStatus = 'error'
      if (deviceDisconnected || /setSignals|Failed to (open|set)|device has been lost/i.test(raw)) {
        connectError = 'Device disconnected — plug it back in and click Connect to retry'
      } else if (/timed out|sync|Failed to connect/i.test(raw)) {
        connectError = `${raw}. Put the device into download mode and try again.`
      } else {
        connectError = raw
      }
    }
  }

  async function flash() {
    if (!esploader || !manifest) return
    flashError = null
    flashProgress = null
    try {
      if (!downloadedBin) {
        flashStatus = 'downloading'
        const entry = manifest.assets[board]
        if (!entry) throw new Error(`No firmware asset for board ${board}`)
        downloadedBin = await loadAsset(entry, (loaded, total) => {
          downloadProgress = { loaded, total }
        })
      }
      flashStatus = 'flashing'
      await esploader.writeFlash({
        fileArray: [{ data: downloadedBin, address: 0x0 }],
        flashSize: 'keep',
        flashMode: 'keep',
        flashFreq: 'keep',
        eraseAll: false,
        compress: true,
        reportProgress: (_idx: number, written: number, total: number) => {
          flashProgress = { written, total }
        },
      })
      // Best-effort reset. For UART-bridged boards RTS toggle works; for
      // native USB CDC boards there's no reliable host-driven reset path
      // (RTS/DTR are virtual lines, EN isn't wired to them), so the UI
      // tells the user to power-cycle in either case.
      const boardEntry = boards.find(b => b.id === board)
      try {
        await esploader.after('hard_reset', boardEntry?.usbOtg ?? false)
      } catch {
        // ignore — best effort
      }
      try { await transport?.disconnect() } catch { /* ignore */ }
      transport = null
      esploader = null
      activePort = null
      chipInfo = null
      connectStatus = 'idle'
      flashStatus = 'done'
    } catch (e) {
      flashStatus = 'error'
      const raw = e instanceof Error ? e.message : String(e)
      flashError = deviceDisconnected || /setSignals|Failed to (open|set)|device has been lost/i.test(raw)
        ? 'Device disconnected mid-flash — the firmware on the device is now in an unknown state. Plug it back in, click Connect, and re-flash.'
        : raw
    }
  }

  async function flashAnother() {
    try {
      await transport?.disconnect()
    } catch {
      // ignore errors during disconnect
    }
    transport = null
    esploader = null
    chipInfo = null
    connectError = null
    connectStatus = 'idle'
    flashStatus = 'idle'
    flashError = null
  }
</script>


<main>
  <Brand title="TaipanMiner Flasher">
    <p class="subtitle">Flash factory firmware to your miner over USB. Chrome / Edge required.</p>
  </Brand>

  {#if !webSerialAvailable}
    <div class="banner danger">
      Your browser doesn't support Web Serial. Use Chrome, Edge, or Opera on desktop.
    </div>
  {/if}

  {#if deviceDisconnected}
    <div class="banner warning">
      Device disconnected. Plug it back in and click Connect to retry.
    </div>
  {/if}

  <section class="card" class:disabled={!webSerialAvailable}>
    <h2>1. Select your board</h2>
    <p class="muted">We can't auto-detect which TaipanMiner variant you have — pick it from the list.</p>
    <label>
      <span>Board</span>
      <Select
        bind:value={board}
        options={boardOptions}
        placeholder="Choose a board…"
        disabled={connectStatus === 'connected' || !webSerialAvailable}
      />
    </label>
  </section>

  <section class="card" class:disabled={!board || !webSerialAvailable}>
    <h2>2. Connect device</h2>
    <p class="muted">Plug your miner into a USB port, then click Connect to grant browser access.</p>
    <p class="hint">If Connect fails or times out, put the device into download mode and try again.</p>
    <div class="row">
      <button
        class="btn primary"
        onclick={connect}
        disabled={!board || !webSerialAvailable || connectStatus === 'connecting' || connectStatus === 'connected'}
      >
        {#if connectStatus === 'connecting'}
          Connecting…
        {:else if connectStatus === 'connected'}
          ✓ Connected
        {:else}
          Connect device
        {/if}
      </button>
      {#if connectStatus === 'connected'}
        <button
          class="btn outline"
          onclick={disconnect}
          disabled={flashStatus === 'downloading' || flashStatus === 'flashing'}
        >Disconnect</button>
      {/if}
    </div>
    {#if connectError}
      <p class="error-msg">{connectError}</p>
    {/if}
    {#if chipInfo}
      <dl class="chip-info">
        <dt>Chip</dt><dd>{chipInfo.chip}</dd>
        <dt>MAC</dt><dd>{chipInfo.mac}</dd>
        <dt>Flash</dt><dd>{chipInfo.flashSize}</dd>
      </dl>
      {#if !chipInfo.chip.toLowerCase().includes('s3')}
        <p class="warn-msg">Detected chip ({chipInfo.chip}) doesn't match the expected ESP32-S3 family for this board.</p>
      {/if}
    {/if}
  </section>

  <section class="card" class:disabled={connectStatus !== 'connected' || !manifest}>
    <h2>3. Flash firmware</h2>
    {#if manifestError}
      <p class="error-msg">{manifestError}</p>
    {/if}
    {#if !manifest}
      <p class="muted">Firmware manifest is loading...</p>
    {:else if !board}
      <p class="muted">Select a board to see what will be flashed.</p>
    {/if}

    {#if flashStatus === 'done'}
      <button class="btn primary" onclick={flashAnother}>
        Flash another device
      </button>
    {:else}
      <button
        class="btn primary"
        onclick={flash}
        disabled={connectStatus !== 'connected' || !manifest || flashStatus === 'downloading' || flashStatus === 'flashing'}
      >
        {#if flashStatus === 'downloading'}
          Loading firmware…
        {:else if flashStatus === 'flashing'}
          Flashing…
        {:else}
          Flash
        {/if}
      </button>
    {/if}

    {#if flashStatus === 'downloading' || flashStatus === 'flashing'}
      {@const pct = flashStatus === 'downloading'
        ? (downloadProgress ? 100 * downloadProgress.loaded / downloadProgress.total : 0)
        : (flashProgress ? 100 * flashProgress.written / flashProgress.total : 0)}
      <div class="progress-block">
        <div class="progress"><div class="progress-fill" style="width: {pct}%"></div></div>
        <div class="progress-msg">
          {flashStatus === 'downloading' ? 'Loading firmware' : 'Flashing'}… {pct.toFixed(0)}%
        </div>
      </div>
    {/if}

    {#if flashStatus === 'done'}
      <p class="success-msg">✓ Flash complete. Power-cycle the device (unplug/replug or press its reset button) to boot the new firmware.</p>
    {/if}
    {#if flashError}
      <p class="error-msg">{flashError}</p>
    {/if}

    {#if manifest && board && manifest.assets[board]}
      <dl class="chip-info">
        <dt>Release</dt><dd>{manifest.tag}</dd>
        <dt>Asset</dt><dd>{manifest.assets[board].file}</dd>
        <dt>Size</dt><dd>{(manifest.assets[board].size / 1024 / 1024).toFixed(2)} MB</dd>
      </dl>
    {/if}
  </section>

  <footer>
    <span>Firmware {manifest?.tag ?? '—'}</span>
    <a href="https://github.com/dangernoodle-io/TaipanMiner" target="_blank" rel="noopener noreferrer" class="github-link" aria-label="View on GitHub">
      <svg viewBox="0 0 24 24" width="16" height="16" fill="currentColor" aria-hidden="true">
        <path d="M12 .5C5.65.5.5 5.65.5 12c0 5.08 3.29 9.39 7.86 10.91.58.11.79-.25.79-.56 0-.27-.01-.99-.02-1.95-3.2.69-3.87-1.54-3.87-1.54-.52-1.32-1.27-1.67-1.27-1.67-1.04-.71.08-.7.08-.7 1.15.08 1.76 1.18 1.76 1.18 1.02 1.76 2.69 1.25 3.34.96.1-.74.4-1.25.72-1.54-2.55-.29-5.24-1.28-5.24-5.69 0-1.26.45-2.29 1.18-3.1-.12-.29-.51-1.46.11-3.05 0 0 .96-.31 3.16 1.18.92-.26 1.9-.39 2.88-.39.98 0 1.96.13 2.88.39 2.2-1.49 3.16-1.18 3.16-1.18.62 1.59.23 2.76.11 3.05.74.81 1.18 1.84 1.18 3.1 0 4.42-2.69 5.4-5.26 5.68.41.36.78 1.06.78 2.14 0 1.55-.01 2.8-.01 3.18 0 .31.21.68.8.56C20.71 21.39 24 17.08 24 12c0-6.35-5.15-11.5-12-11.5z"/>
      </svg>
      <span>View on GitHub</span>
    </a>
  </footer>
</main>

<style>
  main {
    max-width: 720px;
    margin: 0 auto;
    padding: 2rem 1.5rem 4rem;
    display: flex;
    flex-direction: column;
    gap: 1.25rem;
  }

  h2 {
    font-size: 1.1rem;
    font-weight: 600;
    margin-bottom: 0.5rem;
  }

  .card {
    transition: opacity 0.15s;
  }

  label {
    display: flex;
    flex-direction: column;
    gap: 6px;
  }

  .row {
    display: flex;
    gap: 8px;
    align-items: center;
  }

  label span {
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-size: 11px;
  }

  .chip-info {
    display: grid;
    grid-template-columns: max-content 1fr;
    gap: 4px 14px;
    margin-top: 12px;
    font-size: 12px;
    font-variant-numeric: tabular-nums;
  }

  .chip-info dt {
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    font-size: 11px;
  }

  .chip-info dd {
    margin: 0;
    color: var(--text);
  }

  footer {
    margin-top: 1.5rem;
    padding-top: 1rem;
    border-top: 1px solid var(--border);
    display: flex;
    align-items: center;
    justify-content: space-between;
    gap: 1rem;
    color: var(--footer);
    font-size: 12px;
  }

  .github-link {
    display: inline-flex;
    align-items: center;
    gap: 6px;
    color: var(--label);
    transition: color 0.15s;
  }

  .github-link:hover {
    color: var(--accent);
  }

  .progress-msg {
    margin-top: 8px;
    color: var(--label);
    font-size: 12px;
    font-variant-numeric: tabular-nums;
  }

</style>
