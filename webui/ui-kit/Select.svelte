<script lang="ts" generics="T extends string | number">
  type Option = { value: T; label: string }

  let {
    value,
    onchange,
    options,
    disabled = false,
    placeholder
  }: {
    value: T | ''
    onchange?: (v: T | '') => void
    options: Option[]
    disabled?: boolean
    placeholder?: string
  } = $props()
</script>

<select {value} {disabled} onchange={(e) => onchange?.((e.currentTarget as HTMLSelectElement).value as T | '')}>
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
