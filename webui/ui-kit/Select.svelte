<script lang="ts" generics="T extends string | number">
  type Option = { value: T; label: string }

  let {
    value = $bindable(),
    options,
    disabled = false,
    placeholder
  }: {
    value: T | ''
    options: Option[]
    disabled?: boolean
    placeholder?: string
  } = $props()
</script>

<select bind:value {disabled}>
  {#if placeholder}
    <option value="" disabled>{placeholder}</option>
  {/if}
  {#each options as opt (opt.value)}
    <option value={opt.value}>{opt.label}</option>
  {/each}
</select>

<style>
  select {
    padding: 7px 10px;
    background: var(--input);
    color: var(--text);
    border: 1px solid var(--border);
    border-radius: 4px;
    font-size: 12px;
    font-family: inherit;
    font-variant-numeric: tabular-nums;
    transition: border-color 0.15s;
    cursor: pointer;
  }
  select:focus {
    outline: none;
    border-color: var(--accent);
  }
  select:disabled {
    opacity: 0.6;
    cursor: not-allowed;
  }
</style>
