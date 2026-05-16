<script lang="ts">
  import { fan, fanEditOpen } from '../lib/stores'
  import { fetchFan, patchFan, type FanPatch } from '../lib/api'
  import Toggle from './Toggle.svelte'

  type SliderCfg = {
    id: string
    label: string
    suffix: string
    min: number
    max: number
    key: 'die_target_c' | 'vr_target_c' | 'min_pct' | 'manual_pct'
    // true: enabled only when autofan is ON; false: enabled only when autofan is OFF
    autofanOn: boolean
  }

  const SLIDERS: readonly SliderCfg[] = [
    { id: 'fan-die',    label: 'Die target',         suffix: '°C', min: 35, max: 85,  key: 'die_target_c', autofanOn: true  },
    { id: 'fan-vr',     label: 'VR target',          suffix: '°C', min: 50, max: 100, key: 'vr_target_c',  autofanOn: true  },
    { id: 'fan-min',    label: 'Minimum fan speed',  suffix: '%',  min: 0,  max: 100, key: 'min_pct',      autofanOn: true  },
    { id: 'fan-manual', label: 'Fan speed',          suffix: '%',  min: 0,  max: 100, key: 'manual_pct',   autofanOn: false },
  ] as const

  let autofan = false
  let values: Record<string, number> = { 'fan-die': 60, 'fan-vr': 75, 'fan-min': 35, 'fan-manual': 80 }
  let originals: Record<string, number> = { ...values }
  let saving = false
  let msg = ''
  let kind: '' | 'ok' | 'err' = ''

  // Seed form fields ONCE per open transition, the first time both the dialog
  // is open AND $fan is available. Reseeding on every $fan poll would clobber
  // an in-progress drag.
  let seeded = false
  $: {
    if ($fanEditOpen && $fan && !seeded) {
      autofan = $fan.autofan
      values = SLIDERS.reduce(
        (acc, s) => ({ ...acc, [s.id]: $fan![s.key] }),
        {} as Record<string, number>,
      )
      originals = { ...values }
      msg = ''
      kind = ''
      seeded = true
    }
    if (!$fanEditOpen) seeded = false
  }

  function originalPct(s: SliderCfg): number {
    return ((originals[s.id] - s.min) / (s.max - s.min)) * 100
  }

  function setVal(id: string, v: number) {
    values = { ...values, [id]: v }
  }

  async function save() {
    if (!$fan) return
    saving = true
    msg = ''
    kind = ''
    const patch: FanPatch = {}
    if (autofan !== $fan.autofan) patch.autofan = autofan
    for (const s of SLIDERS) {
      if (values[s.id] !== $fan[s.key]) patch[s.key] = values[s.id]
    }
    try {
      await patchFan(patch)
      fan.set(await fetchFan())
      kind = 'ok'
      msg = 'Saved'
      setTimeout(close, 400)
    } catch (e) {
      kind = 'err'
      msg = (e as Error).message
    } finally {
      saving = false
    }
  }

  function close() {
    if (!saving) fanEditOpen.set(false)
  }
</script>

{#if $fanEditOpen}
  <div class="modal-backdrop" onclick={close} role="presentation"></div>
  <div class="modal-panel dialog" role="dialog" aria-modal="true" aria-labelledby="fan-edit-title">
    <form class="setup-form" onsubmit={(e) => { e.preventDefault(); save() }}>
      <section>
        <h2 id="fan-edit-title">Fan</h2>

        <div class="opt-row">
          <span class="opt-label">
            Autofan
            <span class="mode-badge" data-mode={autofan ? 'auto' : 'manual'}>
              {autofan ? 'Closed-loop PID' : 'Manual'}
            </span>
          </span>
          <Toggle bind:checked={autofan} disabled={saving} size="sm" />
        </div>

        {#each SLIDERS as s (s.id)}
          {@const disabled = saving || (s.autofanOn ? !autofan : autofan)}
          {@const moved = values[s.id] !== originals[s.id]}
          <div class="slider-group" class:disabled>
            <div class="slider-head">
              <label for={s.id}>{s.label}</label>
              <span class="val">
                {#if moved}<span class="orig">{originals[s.id]}{s.suffix}&nbsp;→&nbsp;</span>{/if}{values[s.id]}{s.suffix}
              </span>
            </div>
            <div class="slider-track">
              {#if moved}
                <span class="orig-mark" style="left: {originalPct(s)}%" aria-hidden="true"></span>
              {/if}
              <input
                id={s.id}
                type="range"
                min={s.min}
                max={s.max}
                step="1"
                value={values[s.id]}
                oninput={(e) => setVal(s.id, +e.currentTarget.value)}
                {disabled}
              />
            </div>
          </div>
        {/each}
      </section>

      {#if $fan}
        <p class="live-line">
          Live: {$fan.duty_pct ?? '—'}% · {$fan.rpm ?? '—'} rpm
          {#if $fan.autofan && $fan.pid_input_src}
            · PID following {$fan.pid_input_src === 'die' ? 'ASIC' : $fan.pid_input_src.toUpperCase()}
            {#if $fan.pid_input_c != null} ({$fan.pid_input_c.toFixed(1)}°C){/if}
          {/if}
        </p>
      {/if}

      <div class="actions">
        <button type="button" class="btn outline" onclick={close} disabled={saving}>Cancel</button>
        <button type="submit" class="btn primary" disabled={saving || !$fan}>
          {saving ? 'Saving…' : 'Save'}
        </button>
      </div>
      {#if msg}<div class="msg" class:err={kind === 'err'}>{msg}</div>{/if}
    </form>
  </div>
{/if}

<style>
  .dialog {
    position: fixed;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    padding: 22px 24px;
    width: min(420px, calc(100vw - 32px));
    max-height: calc(100vh - 32px);
    overflow-y: auto;
    z-index: 41;
  }

  .live-line {
    margin: 0;
    font-size: 11px;
    color: var(--muted);
    line-height: 1.5;
  }

  .slider-track {
    position: relative;
    width: 100%;
  }

  .slider-track input[type="range"] {
    position: relative;
    z-index: 1;
  }

  .orig-mark {
    position: absolute;
    top: 50%;
    width: 2px;
    height: 14px;
    background: var(--accent);
    transform: translate(-50%, -50%);
    pointer-events: none;
    z-index: 0;
  }

  .orig {
    color: var(--muted);
    font-weight: normal;
  }
</style>
