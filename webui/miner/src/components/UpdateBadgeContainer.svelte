<script lang="ts">
  import { getContext, onMount } from 'svelte'
  import UpdateBadge from './UpdateBadge.svelte'
  import { createUpdateAvailableState, UPDATE_AVAILABLE_TOPIC } from '../lib/updateAvailableState.svelte'
  import { EVENT_BUS_KEY, type EventBus } from '../lib/eventBus.svelte'

  /* Owns its own state machine + subscribes to the SSE bus on mount.
   * No props needed — the App-level chrome just renders <UpdateBadgeContainer />. */
  const state = createUpdateAvailableState()
  const bus = getContext<EventBus | undefined>(EVENT_BUS_KEY)

  onMount(() => bus?.subscribe(UPDATE_AVAILABLE_TOPIC, state.handleMessage))
</script>

<UpdateBadge available={state.available} latest={state.latest} />
