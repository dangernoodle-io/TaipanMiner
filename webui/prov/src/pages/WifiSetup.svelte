<script lang="ts">
  import { onMount } from 'svelte'
  import WifiSelect from 'ui-kit/WifiSelect.svelte'
  import { createWifiSetupState } from '../lib/wifiSetupState.svelte'

  let { onSaved }: { onSaved: () => void } = $props()

  const ws = createWifiSetupState(() => onSaved)

  onMount(() => {
    ws.scan()
  })
</script>

<div class="setup-form">
  {#if ws.submitError}
    <div class="error-banner">
      {ws.submitError}
    </div>
  {/if}

  <form onsubmit={(e) => { e.preventDefault(); ws.handleSubmit() }}>
    <section>
      <h2>WiFi</h2>
      <div class="form-group">
        <span class="label">Network</span>
        <div class="scan-controls">
          <WifiSelect
            networks={ws.networks}
            bind:selected={ws.selectedSsid}
            scanning={ws.scanning}
            error={ws.scanError}
            disabled={ws.submitting}
          />
          <button
            type="button"
            class="rescan-btn"
            onclick={() => ws.scan()}
            disabled={ws.scanning || ws.submitting}
            aria-label="Rescan networks"
          >
            <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">
              <path d="M23 4v6h-6"/>
              <path d="M1 20v-6h6"/>
              <path d="M3.51 9a9 9 0 0114.85-3.36L23 10"/>
              <path d="M20.49 15a9 9 0 01-14.85 3.36L1 14"/>
            </svg>
          </button>
        </div>
        {#if ws.scanError}
          <div class="inline-error">{ws.scanError}</div>
        {/if}
      </div>

      {#if ws.selectedSsid === '__manual__'}
        <div class="manual-entry">
          <input
            type="text"
            bind:value={ws.manualSsid}
            placeholder="Enter SSID"
            maxlength="31"
            disabled={ws.submitting}
          />
          {#if ws.errors.ssid}
            <div class="field-error">{ws.errors.ssid}</div>
          {/if}
        </div>
      {/if}

      <div class="form-group password-wrapper">
        <label for="pass">Password</label>
        <div class="password-input-group">
          <input
            id="pass"
            type={ws.showPass ? 'text' : 'password'}
            bind:value={ws.pass}
            maxlength="63"
            disabled={ws.submitting}
          />
          <button
            type="button"
            class="toggle-pass"
            onclick={(e) => { e.preventDefault(); ws.showPass = !ws.showPass }}
            disabled={ws.submitting}
            aria-label={ws.showPass ? 'Hide password' : 'Show password'}
          >
            {#if ws.showPass}
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" aria-hidden="true">
                <path d="M17.94 17.94A10.07 10.07 0 0112 20c-7 0-11-8-11-8a18.45 18.45 0 015.06-5.94M9.9 4.24A9.12 9.12 0 0112 4c7 0 11 8 11 8a18.5 18.5 0 01-2.16 3.19"/>
                <line x1="1" y1="1" x2="23" y2="23"/>
              </svg>
            {:else}
              <svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" aria-hidden="true">
                <path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"/>
                <circle cx="12" cy="12" r="3"/>
              </svg>
            {/if}
          </button>
        </div>
      </div>
    </section>

    <section>
      <h2>Mining</h2>
      <div class="form-group">
        <label for="hostname">Hostname</label>
        <input
          id="hostname"
          type="text"
          value={ws.hostname}
          oninput={(e) => { ws.hostname = e.currentTarget.value }}
          maxlength="31"
          placeholder="taipan-miner"
          disabled={ws.submitting}
        />
      </div>

      <div class="form-group">
        <label for="wallet">Wallet Address</label>
        <input
          id="wallet"
          type="text"
          bind:value={ws.wallet}
          maxlength="63"
          placeholder="1BTC..."
          disabled={ws.submitting}
        />
        {#if ws.errors.wallet}
          <div class="field-error">{ws.errors.wallet}</div>
        {/if}
      </div>

      <div class="form-group">
        <label for="worker">Worker Name</label>
        <input
          id="worker"
          type="text"
          value={ws.worker}
          oninput={(e) => { ws.worker = e.currentTarget.value }}
          maxlength="31"
          placeholder="miner-1"
          disabled={ws.submitting}
        />
        {#if ws.errors.worker}
          <div class="field-error">{ws.errors.worker}</div>
        {/if}
      </div>
    </section>

    <section>
      <h2>Pool</h2>
      <div class="form-group">
        <label for="pool_host">Host</label>
        <input
          id="pool_host"
          type="text"
          bind:value={ws.poolHost}
          maxlength="63"
          placeholder="pool.example.com"
          disabled={ws.submitting}
        />
        {#if ws.errors.poolHost}
          <div class="field-error">{ws.errors.poolHost}</div>
        {/if}
      </div>

      <div class="form-group">
        <label for="pool_port">Port</label>
        <input
          id="pool_port"
          type="text"
          inputmode="numeric"
          bind:value={ws.poolPort}
          maxlength="5"
          placeholder="3333"
          disabled={ws.submitting}
        />
        {#if ws.errors.poolPort}
          <div class="field-error">{ws.errors.poolPort}</div>
        {/if}
      </div>

      <div class="form-group">
        <label for="pool_pass">Password</label>
        <input
          id="pool_pass"
          type="text"
          bind:value={ws.poolPass}
          maxlength="63"
          placeholder="optional"
          disabled={ws.submitting}
        />
      </div>
    </section>

    <button type="submit" class="submit-btn" disabled={ws.submitting || ws.scanning}>
      {ws.submitting ? 'Saving...' : 'Save & Connect'}
    </button>
  </form>
</div>

<style>
  .setup-form {
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }

  .error-banner {
    background: color-mix(in srgb, var(--danger) 15%, transparent);
    border: 1px solid var(--danger);
    color: var(--danger);
    padding: 0.75rem;
    border-radius: 4px;
    font-size: 13px;
  }

  form {
    display: flex;
    flex-direction: column;
    gap: 1.5rem;
  }

  section {
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }

  h2 {
    font-size: 14px;
    margin: 0;
    color: var(--accent);
    font-weight: 600;
    text-transform: uppercase;
    letter-spacing: 1px;
  }

  .form-group {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }

  label,
  .label {
    font-size: 12px;
    color: var(--label);
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }

  input {
    padding: 12px;
    background: var(--input);
    border: 1px solid var(--border);
    border-radius: 4px;
    color: var(--text);
    font-size: 14px;
    font-family: inherit;
    box-sizing: border-box;
    width: 100%;
  }

  input:focus {
    outline: none;
    border-color: var(--accent);
  }

  input:disabled {
    opacity: 0.6;
    cursor: not-allowed;
  }

  .scan-controls {
    display: flex;
    gap: 8px;
    align-items: stretch;
  }

  .rescan-btn {
    background: var(--input);
    border: 1px solid var(--border);
    color: var(--accent);
    padding: 12px;
    border-radius: 4px;
    cursor: pointer;
    min-height: 44px;
    min-width: 44px;
    flex-shrink: 0;
    transition: border-color 0.2s;
    display: flex;
    align-items: center;
    justify-content: center;
  }

  .rescan-btn:hover:not(:disabled) {
    border-color: var(--accent);
  }

  .rescan-btn:disabled {
    opacity: 0.6;
    cursor: not-allowed;
  }

  .inline-error {
    color: var(--danger);
    font-size: 12px;
    margin-top: 0.25rem;
  }

  .manual-entry {
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
    margin-top: 0.5rem;
  }

  .password-wrapper {
    position: relative;
  }

  .password-input-group {
    position: relative;
    display: flex;
  }

  .password-input-group input {
    flex: 1;
    padding-right: 40px;
  }

  .toggle-pass {
    position: absolute;
    right: 12px;
    top: 50%;
    transform: translateY(-50%);
    background: none;
    border: none;
    color: var(--accent);
    cursor: pointer;
    padding: 0;
    display: flex;
    align-items: center;
    justify-content: center;
    width: 24px;
    height: 24px;
  }

  .toggle-pass:hover:not(:disabled) {
    color: var(--accent-hover);
  }

  .toggle-pass:disabled {
    opacity: 0.6;
    cursor: not-allowed;
  }

  .field-error {
    color: var(--danger);
    font-size: 12px;
    margin-top: 0.25rem;
  }

  .submit-btn {
    background: var(--accent);
    color: var(--bg);
    border: none;
    padding: 14px;
    border-radius: 4px;
    font-size: 14px;
    font-weight: bold;
    cursor: pointer;
    text-transform: uppercase;
    letter-spacing: 1px;
    min-height: 44px;
    margin-top: 10px;
    transition: background 0.2s;
  }

  .submit-btn:hover:not(:disabled) {
    background: var(--accent-hover);
  }

  .submit-btn:disabled {
    opacity: 0.6;
    cursor: not-allowed;
  }
</style>
