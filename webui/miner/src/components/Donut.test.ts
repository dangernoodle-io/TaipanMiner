import { describe, it, expect } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import Donut from './Donut.svelte'

describe('Donut', () => {
  it('renders the label', () => {
    render(Donut, { props: { used: 50, total: 100, label: 'Memory' } })
    expect(screen.getByText('Memory')).toBeInTheDocument()
  })

  it('shows dash when values are null', () => {
    render(Donut, { props: { used: null, total: null, label: 'Mem' } })
    expect(screen.getByText('—')).toBeInTheDocument()
  })

  it('calculates percentage correctly', () => {
    render(Donut, { props: { used: 75, total: 100, label: 'Usage' } })
    expect(screen.getByText('75%')).toBeInTheDocument()
  })

  it('shows byte format by default', () => {
    render(Donut, { props: { used: 512, total: 1024, label: 'RAM' } })
    // 512 B / 1 KB
    expect(screen.getByText(/512 B/)).toBeInTheDocument()
    expect(screen.getByText(/1 KB/)).toBeInTheDocument()
  })

  it('shows percent format when format=percent', () => {
    render(Donut, { props: { used: 50, total: 100, label: 'CPU', format: 'percent' } })
    // Should show raw numbers not byte-formatted
    expect(screen.getByText(/50 \/ 100/)).toBeInTheDocument()
  })

  it('shows KB for values between 1024 and 1MB', () => {
    render(Donut, { props: { used: 2048, total: 4096, label: 'Cache' } })
    expect(screen.getByText(/2 KB/)).toBeInTheDocument()
    expect(screen.getByText(/4 KB/)).toBeInTheDocument()
  })

  it('renders SVG circles', () => {
    render(Donut, { props: { used: 50, total: 100, label: 'X' } })
    const circles = document.querySelectorAll('circle')
    expect(circles.length).toBeGreaterThanOrEqual(2)
  })

  it('shows 0% for zero used', () => {
    render(Donut, { props: { used: 0, total: 100, label: 'Free' } })
    expect(screen.getByText('0%')).toBeInTheDocument()
  })
})
