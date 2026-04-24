<script lang="ts">
  export let checked: boolean
  export let disabled = false

  function onChange(e: Event) {
    const target = e.currentTarget as HTMLInputElement
    checked = target.checked
  }
</script>

<label class="toggle" class:disabled>
  <input type="checkbox" {checked} {disabled} on:change={onChange} on:change />
  <span class="slider"></span>
</label>

<style>
  .toggle {
    position: relative;
    display: inline-block;
    width: 44px;
    height: 24px;
    cursor: pointer;
    flex-shrink: 0;
  }

  .toggle.disabled { cursor: not-allowed; }

  .toggle input {
    opacity: 0;
    width: 0;
    height: 0;
  }

  .slider {
    position: absolute;
    inset: 0;
    background: var(--input);
    border: 1px solid var(--border);
    border-radius: 24px;
    transition: background 0.2s;
  }

  .slider::before {
    content: '';
    position: absolute;
    width: 18px;
    height: 18px;
    left: 2px;
    bottom: 2px;
    background: var(--label);
    border-radius: 50%;
    transition: transform 0.2s, background 0.2s;
  }

  .toggle input:checked + .slider {
    background: var(--accent);
    border-color: var(--accent);
  }

  .toggle input:checked + .slider::before {
    transform: translateX(20px);
    background: var(--bg);
  }

  .toggle.disabled .slider { opacity: 0.5; }
</style>
