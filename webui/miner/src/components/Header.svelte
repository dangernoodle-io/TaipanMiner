<script lang="ts">
  import Header from 'ui-kit/Header.svelte'
  import { formatVersion } from 'ui-kit/version'
  import { info } from '../lib/stores'
  import UpdateBadgeContainer from './UpdateBadgeContainer.svelte'
</script>

<Header
  title="TaipanMiner"
  subtitle={$info ? `${$info.build.board} · ${formatVersion($info.build.version)}` : undefined}
>
  {#snippet right()}
    <UpdateBadgeContainer />
  {/snippet}
</Header>
