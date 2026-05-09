import { describe, it, expect } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import ChipsCard from './ChipsCard.svelte'
import type { Chip } from '../lib/api'

function makeChip(overrides: Partial<Chip> = {}): Chip {
  return {
    idx: 0,
    total_ghs: 120,
    error_ghs: 0,
    hw_err_pct: 0.01,
    total_raw: 1000,
    error_raw: 0,
    domain_ghs: [30, 30, 30, 30],
    total_drops: 0,
    error_drops: 0,
    domain_drops: [0, 0, 0, 0],
    last_drop_ago_s: null,
    ...overrides
  }
}

describe('ChipsCard', () => {
  it('renders card header', () => {
    render(ChipsCard, { props: { chips: [makeChip()] } })
    expect(screen.getByText('Chips')).toBeInTheDocument()
  })

  it('renders chip index', () => {
    render(ChipsCard, { props: { chips: [makeChip({ idx: 2 })] } })
    expect(screen.getByText('chip 2')).toBeInTheDocument()
  })

  it('renders GH/s rate', () => {
    render(ChipsCard, { props: { chips: [makeChip({ total_ghs: 120 })] } })
    expect(screen.getByText('120.0')).toBeInTheDocument()
  })

  it('renders error percentage', () => {
    render(ChipsCard, { props: { chips: [makeChip({ hw_err_pct: 0.05 })] } })
    expect(screen.getByText('0.05% err')).toBeInTheDocument()
  })

  it('renders multiple chips', () => {
    const chips = [makeChip({ idx: 0 }), makeChip({ idx: 1 })]
    render(ChipsCard, { props: { chips } })
    expect(screen.getByText('chip 0')).toBeInTheDocument()
    expect(screen.getByText('chip 1')).toBeInTheDocument()
  })

  it('shows domain rows', () => {
    render(ChipsCard, { props: { chips: [makeChip({ domain_ghs: [10, 20, 30, 40] })] } })
    expect(screen.getByText('D0')).toBeInTheDocument()
    expect(screen.getByText('D3')).toBeInTheDocument()
  })

  it('marks corrupt when last_drop_ago_s < 300', () => {
    render(ChipsCard, { props: { chips: [makeChip({ last_drop_ago_s: 60 })] } })
    expect(document.querySelector('.chip.corrupt')).not.toBeNull()
    // chip-warning span (not the legend) shows telem corrupt
    expect(document.querySelector('.chip-warning')).not.toBeNull()
  })

  it('marks inactive when total_ghs < 1', () => {
    render(ChipsCard, { props: { chips: [makeChip({ total_ghs: 0, last_drop_ago_s: null })] } })
    expect(document.querySelector('.chip.inactive')).not.toBeNull()
  })

  it('shows lifetime drops count', () => {
    render(ChipsCard, { props: { chips: [makeChip({ total_drops: 5, last_drop_ago_s: 400 })] } })
    expect(screen.getByText('5 drops')).toBeInTheDocument()
  })

  it('renders legend', () => {
    render(ChipsCard, { props: { chips: [] } })
    expect(screen.getByText(/telem corrupt/)).toBeInTheDocument()
    expect(screen.getByText(/inactive/)).toBeInTheDocument()
    expect(screen.getByText(/healthy/)).toBeInTheDocument()
  })
})
