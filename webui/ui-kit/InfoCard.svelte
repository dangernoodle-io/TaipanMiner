<script lang="ts">
  import type { Snippet } from 'svelte'

  // A titled card containing a definition list of label/value rows.
  // Compose with <InfoRow> children so every key/value block across the app
  // shares one consistent typography + layout.
  let { title, children }: { title: string; children: Snippet } = $props()
</script>

<section class="card">
  <h3>{title}</h3>
  <dl>{@render children()}</dl>
</section>

<style>
  /* card chrome + h3 typography come from ui-kit utilities.css (.card / h3). */
  h3 { margin-bottom: 12px; }

  dl { margin: 0; }

  /* drop the dotted separator under the final row */
  dl :global(.info-row:last-child) { border-bottom: none; }
</style>
