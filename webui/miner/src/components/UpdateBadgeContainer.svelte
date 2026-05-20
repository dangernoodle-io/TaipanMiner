<script lang="ts">
  import UpdateBadge from './UpdateBadge.svelte'

  interface Props {
    updateState: { available: boolean; latest: string | null }
  }
  let { updateState }: Props = $props()
</script>

<UpdateBadge available={updateState.available} latest={updateState.latest} />
