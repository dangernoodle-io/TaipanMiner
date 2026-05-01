<script lang="ts">
  import { rebooting } from '../lib/stores'
</script>

{#if $rebooting.active}
  <div class="modal-backdrop reboot-backdrop" role="alert" aria-live="polite" aria-busy="true">
    <div class="modal-panel reboot-panel">
      {#if $rebooting.timedOut}
        <div class="icon timed-out">!</div>
        <h3>Miner not responding</h3>
        <p>
          Waited over 90 seconds after {$rebooting.reason}. The device may be
          stuck during boot, WiFi reconnect, or an OTA rollback. Check the
          serial console or power-cycle manually.
        </p>
      {:else}
        <div class="spinner lg"></div>
        <h3>{$rebooting.reason}</h3>
        <p>Waiting for the miner to come back… {$rebooting.elapsed}s</p>
      {/if}
    </div>
  </div>
{/if}

<style>
  .reboot-backdrop {
    display: flex;
    align-items: center;
    justify-content: center;
    background: rgba(0, 0, 0, 0.6);
    z-index: 100;
  }

  .reboot-panel {
    padding: 32px 36px;
    width: min(420px, calc(100vw - 32px));
    text-align: center;
  }

  .icon {
    width: 42px;
    height: 42px;
    border-radius: 50%;
    display: flex;
    align-items: center;
    justify-content: center;
    margin: 0 auto 18px;
    font-size: 22px;
    font-weight: 600;
  }

  .icon.timed-out {
    background: rgba(231, 76, 60, 0.15);
    color: var(--danger);
    border: 2px solid var(--danger);
  }

  h3 {
    margin: 0 0 10px 0;
    color: var(--text);
    font-size: 16px;
    font-weight: 600;
  }

  p {
    margin: 0;
    color: var(--label);
    font-size: 13px;
    line-height: 1.5;
  }
</style>
