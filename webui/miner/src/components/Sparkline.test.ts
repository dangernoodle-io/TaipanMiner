import { describe, it, expect } from 'vitest'
import { render } from '@testing-library/svelte'
import Sparkline from './Sparkline.svelte'

describe('Sparkline', () => {
  it('renders an SVG element', () => {
    render(Sparkline, { props: { points: [1, 2, 3] } })
    expect(document.querySelector('svg.sparkline')).not.toBeNull()
  })

  it('renders one circle per data point', () => {
    render(Sparkline, { props: { points: [10, 20, 30, 40] } })
    const circles = document.querySelectorAll('circle')
    expect(circles.length).toBe(4)
  })

  it('renders a polyline', () => {
    render(Sparkline, { props: { points: [1, 2, 3] } })
    expect(document.querySelector('polyline')).not.toBeNull()
  })

  it('uses custom width and height', () => {
    render(Sparkline, { props: { points: [1, 2], width: 160, height: 40 } })
    const svg = document.querySelector('svg')!
    expect(svg.getAttribute('width')).toBe('160')
    expect(svg.getAttribute('height')).toBe('40')
  })

  it('handles single-point array without error', () => {
    render(Sparkline, { props: { points: [5] } })
    const circles = document.querySelectorAll('circle')
    expect(circles.length).toBe(1)
  })

  it('positions single point at center x', () => {
    // getX returns width/2 when n===1 — exercises the n===1 branch
    render(Sparkline, { props: { points: [5], width: 80 } })
    const circle = document.querySelector('circle')!
    expect(Number(circle.getAttribute('cx'))).toBeCloseTo(40)
  })

  it('applies custom color via style attribute', () => {
    // Pass a named color to avoid jsdom hex→rgb normalization
    render(Sparkline, { props: { points: [1, 2], color: 'red' } })
    const polyline = document.querySelector('polyline')!
    expect(polyline.getAttribute('style')).toContain('red')
  })
})
