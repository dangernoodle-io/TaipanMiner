<script lang="ts">
  import { stats, info, thermal } from '../lib/stores'

  $effect(() => {
    if ($stats) {
      const asicTempStr = ($thermal?.asic.present && $thermal.asic.c != null)
        ? $thermal.asic.c.toFixed(0)
        : '—'
      document.title = `TaipanMiner • ${$info?.build.board ?? '—'} • ${$stats.asic_total_ghs?.toFixed(1) || '—'} GH/s • ${asicTempStr}°C`
    }
  })
</script>
