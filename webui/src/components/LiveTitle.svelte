<script lang="ts">
  import { stats, info } from '../lib/stores'

  $: if ($stats) {
    document.title = `TaipanMiner • ${$info?.board ?? '—'} • ${$stats.asic_total_ghs?.toFixed(1) || '—'} GH/s • ${$stats.asic_temp_c?.toFixed(0) || '—'}°C`
  }
</script>
