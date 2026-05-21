<script lang="ts">
  import { fmtRelativeFromUnixTs } from '../lib/fmt'

  interface BlockFoundInfo {
    host: string
    port: number
    share_diff?: number
    timestamp?: number
  }

  interface Props {
    state: {
      visible: boolean
      lastFound: BlockFoundInfo | null
      dismiss: () => void
    }
  }

  let { state }: Props = $props()
</script>

{#if state.visible && state.lastFound}
  <div class="block-found-banner" role="status" aria-live="polite">
    <span class="banner-content">
      🎉 Block found! Pool {state.lastFound.host}:{state.lastFound.port}
      {#if state.lastFound.share_diff != null}
        &nbsp;· diff {state.lastFound.share_diff.toFixed(2)}
      {/if}
      {#if state.lastFound.timestamp}
        &nbsp;· {fmtRelativeFromUnixTs(state.lastFound.timestamp)}
      {/if}
    </span>
    <button class="dismiss-btn" onclick={state.dismiss} aria-label="Dismiss block found notification">×</button>
  </div>
{/if}

<style>
  .block-found-banner {
    display: flex;
    align-items: center;
    justify-content: space-between;
    width: 100%;
    padding: 8px 14px;
    border-radius: 999px;
    background: #1a7a3c;
    color: #fff;
    font-size: 13px;
    font-weight: 600;
    margin-bottom: 12px;
    box-sizing: border-box;
  }

  .banner-content {
    flex: 1;
    min-width: 0;
    overflow: hidden;
    text-overflow: ellipsis;
    white-space: nowrap;
  }

  .dismiss-btn {
    background: none;
    border: none;
    color: #fff;
    font-size: 18px;
    line-height: 1;
    cursor: pointer;
    padding: 0 0 0 10px;
    flex-shrink: 0;
    opacity: 0.8;
    transition: opacity 0.15s;
  }

  .dismiss-btn:hover {
    opacity: 1;
  }
</style>
