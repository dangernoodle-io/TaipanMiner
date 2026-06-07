<script lang="ts">
  interface Props {
    points: number[]
    width?: number
    height?: number
    color?: string
  }
  let { points, width = 80, height = 24, color = 'var(--accent)' }: Props = $props()

  const maxVal = $derived(Math.max(...points, 0.0001))
  const minVal = $derived(Math.min(...points, 0))
  const range = $derived(Math.max(maxVal - minVal, 0.0001))

  function getX(i: number, n: number): number {
    if (n === 1) return width / 2
    return (i / (n - 1)) * width
  }

  function getY(val: number): number {
    return height - ((val - minVal) / range) * height
  }

  const polylinePoints = $derived(
    points
      .map((val, i) => `${getX(i, points.length)},${getY(val)}`)
      .join(' ')
  )
</script>

<svg {width} {height} viewBox={`0 0 ${width} ${height}`} class="sparkline">
  <polyline points={polylinePoints} style="stroke: {color}" />
  {#each points as p, i}
    <circle cx={getX(i, points.length)} cy={getY(p)} r="2" style="fill: {color}" />
  {/each}
</svg>

<style>
  .sparkline {
    overflow: visible;
  }

  .sparkline polyline {
    fill: none;
    stroke-width: 1.5;
    stroke-linecap: round;
    stroke-linejoin: round;
  }
</style>
