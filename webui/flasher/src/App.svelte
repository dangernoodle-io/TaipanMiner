<script lang="ts">
  import Brand from 'ui-kit/Brand.svelte'
  import Select from 'ui-kit/Select.svelte'
  import { createFlashState } from './lib/flashState.svelte'

  const webSerialAvailable = typeof navigator !== 'undefined' && 'serial' in navigator

  const state = createFlashState()

  $effect(() => {
    state.loadManifestAction()
  })

  $effect(() => {
    const releasePort = () => {
      state.transport?.disconnect().catch(() => {})
    }
    const onDisconnect = (e: Event) => {
      const target = (e as unknown as { target: unknown }).target
      state.handleDeviceDisconnect(target)
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

  {#if state.deviceDisconnected}
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
        value={state.board}
        onchange={(v) => state.selectBoard(v as string)}
        options={state.boardOptions}
        placeholder="Choose a board…"
        disabled={state.connectStatus === 'connected' || !webSerialAvailable}
      />
    </label>
  </section>

  <section class="card" class:disabled={!state.board || !webSerialAvailable}>
    <h2>2. Connect device</h2>
    <p class="muted">Plug your miner into a USB port, then click Connect to grant browser access.</p>
    <p class="hint">If Connect fails or times out, put the device into download mode and try again.</p>
    <div class="row">
      <button
        class="btn primary"
        onclick={() => state.connect()}
        disabled={!state.board || !webSerialAvailable || state.connectStatus === 'connecting' || state.connectStatus === 'connected'}
      >
        {#if state.connectStatus === 'connecting'}
          Connecting…
        {:else if state.connectStatus === 'connected'}
          ✓ Connected
        {:else}
          Connect device
        {/if}
      </button>
      {#if state.connectStatus === 'connected'}
        <button
          class="btn outline"
          onclick={() => state.disconnect()}
          disabled={state.flashStatus === 'downloading' || state.flashStatus === 'flashing'}
        >Disconnect</button>
      {/if}
    </div>
    {#if state.connectError}
      <p class="error-msg">{state.connectError}</p>
    {/if}
    {#if state.chipInfo}
      <dl class="chip-info">
        <dt>Chip</dt><dd>{state.chipInfo.chip}</dd>
        <dt>MAC</dt><dd>{state.chipInfo.mac}</dd>
        <dt>Flash</dt><dd>{state.chipInfo.flashSize}</dd>
      </dl>
      {#if !state.chipInfo.chip.toLowerCase().includes('s3')}
        <p class="warn-msg">Detected chip ({state.chipInfo.chip}) doesn't match the expected ESP32-S3 family for this board.</p>
      {/if}
    {/if}
  </section>

  <section class="card" class:disabled={state.connectStatus !== 'connected' || !state.manifest}>
    <h2>3. Flash firmware</h2>
    {#if state.manifestError}
      <p class="error-msg">{state.manifestError}</p>
    {/if}
    {#if !state.manifest}
      <p class="muted">Firmware manifest is loading...</p>
    {:else if !state.board}
      <p class="muted">Select a board to see what will be flashed.</p>
    {/if}

    {#if state.flashStatus === 'done'}
      <button class="btn primary" onclick={() => state.flashAnother()}>
        Flash another device
      </button>
    {:else}
      <button
        class="btn primary"
        onclick={() => state.flash()}
        disabled={state.connectStatus !== 'connected' || !state.manifest || state.flashStatus === 'downloading' || state.flashStatus === 'flashing'}
      >
        {#if state.flashStatus === 'downloading'}
          Loading firmware…
        {:else if state.flashStatus === 'flashing'}
          Flashing…
        {:else}
          Flash
        {/if}
      </button>
    {/if}

    {#if state.flashStatus === 'downloading' || state.flashStatus === 'flashing'}
      {@const pct = state.flashStatus === 'downloading'
        ? (state.downloadProgress ? 100 * state.downloadProgress.loaded / state.downloadProgress.total : 0)
        : (state.flashProgress ? 100 * state.flashProgress.written / state.flashProgress.total : 0)}
      <div class="progress-block">
        <div class="progress"><div class="progress-fill" style="width: {pct}%"></div></div>
        <div class="progress-msg">
          {state.flashStatus === 'downloading' ? 'Loading firmware' : 'Flashing'}… {pct.toFixed(0)}%
        </div>
      </div>
    {/if}

    {#if state.flashStatus === 'done'}
      <p class="success-msg">✓ Flash complete. Power-cycle the device (unplug/replug or press its reset button) to boot the new firmware.</p>
    {/if}
    {#if state.flashError}
      <p class="error-msg">{state.flashError}</p>
    {/if}

    {#if state.manifest && state.board && state.manifest.assets[state.board]}
      <dl class="chip-info">
        <dt>Release</dt><dd>{state.manifest.tag}</dd>
        <dt>Asset</dt><dd>{state.manifest.assets[state.board].file}</dd>
        <dt>Size</dt><dd>{(state.manifest.assets[state.board].size / 1024 / 1024).toFixed(2)} MB</dd>
      </dl>
    {/if}
  </section>

  <footer>
    <span>Firmware {state.manifest?.tag ?? '—'}</span>
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
