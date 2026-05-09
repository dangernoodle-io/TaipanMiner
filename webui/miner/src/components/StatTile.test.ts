import { describe, it, expect } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import StatTile from './StatTile.svelte'

describe('StatTile', () => {
  it('renders label', () => {
    render(StatTile, { props: { label: 'Hashrate', value: 100 } })
    expect(screen.getByText('Hashrate')).toBeInTheDocument()
  })

  it('shows dash when value is null', () => {
    render(StatTile, { props: { label: 'Temp', value: null } })
    expect(screen.getByText('—')).toBeInTheDocument()
  })

  it('shows value with one decimal for numbers < 100', () => {
    render(StatTile, { props: { label: 'Temp', value: 72.5 } })
    expect(screen.getByText('72.5')).toBeInTheDocument()
  })

  it('shows value with zero decimal for numbers >= 100', () => {
    render(StatTile, { props: { label: 'Rate', value: 485 } })
    expect(screen.getByText('485')).toBeInTheDocument()
  })

  it('renders unit when provided', () => {
    render(StatTile, { props: { label: 'Temp', value: 72, unit: '°C' } })
    expect(screen.getByText('°C')).toBeInTheDocument()
  })

  it('renders string values as-is', () => {
    render(StatTile, { props: { label: 'Status', value: 'ok' } })
    expect(screen.getByText('ok')).toBeInTheDocument()
  })

  it('sets data-status to danger when numeric >= danger threshold', () => {
    render(StatTile, { props: { label: 'Temp', value: 90, danger: 85 } })
    const tile = document.querySelector('.tile')!
    expect(tile.getAttribute('data-status')).toBe('danger')
  })

  it('sets data-status to warn when numeric >= warn threshold', () => {
    render(StatTile, { props: { label: 'Temp', value: 76, warn: 75 } })
    const tile = document.querySelector('.tile')!
    expect(tile.getAttribute('data-status')).toBe('warn')
  })

  it('sets data-status to empty when below all thresholds', () => {
    render(StatTile, { props: { label: 'Temp', value: 60, warn: 75, danger: 85 } })
    const tile = document.querySelector('.tile')!
    expect(tile.getAttribute('data-status')).toBe('')
  })

  it('respects explicit flag prop', () => {
    render(StatTile, { props: { label: 'Temp', value: 50, flag: 'danger' } })
    const tile = document.querySelector('.tile')!
    expect(tile.getAttribute('data-status')).toBe('danger')
  })
})
