<script lang="ts">
  import { stats } from '../lib/stores'

  $: if ($stats) {
    document.title = `TaipanMiner • ${$stats.board} • ${$stats.asic_total_ghs?.toFixed(1) || '—'} GH/s • ${$stats.asic_temp_c?.toFixed(0) || '—'}°C`
  }
</script>
