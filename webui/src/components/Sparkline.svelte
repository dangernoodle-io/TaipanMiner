<script lang="ts">
  export let points: number[]
  export let width = 80
  export let height = 24
  export let color = 'var(--accent)'

  $: maxVal = Math.max(...points, 0.0001)
  $: minVal = Math.min(...points, 0)
  $: range = Math.max(maxVal - minVal, 0.0001)

  function getX(i: number, n: number): number {
    if (n === 1) return width / 2
    return (i / (n - 1)) * width
  }

  function getY(val: number): number {
    return height - ((val - minVal) / range) * height
  }

  $: polylinePoints = points
    .map((val, i) => `${getX(i, points.length)},${getY(val)}`)
    .join(' ')
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
