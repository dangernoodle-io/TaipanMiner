import { describe, it, expect } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import RollingRates from './RollingRates.svelte'

describe('RollingRates', () => {
  it('renders 1m/10m/1h column headers', () => {
    render(RollingRates, { props: {} })
    expect(screen.getByText('1m')).toBeInTheDocument()
    expect(screen.getByText('10m')).toBeInTheDocument()
    expect(screen.getByText('1h')).toBeInTheDocument()
  })

  it('renders dash values when all null', () => {
    render(RollingRates, { props: { ghs1m: null, ghs10m: null, ghs1h: null } })
    const dashes = screen.getAllByText('—')
    expect(dashes.length).toBeGreaterThanOrEqual(3)
  })

  it('renders formatted GH/s values', () => {
    render(RollingRates, { props: { ghs1m: 485, ghs10m: 490, ghs1h: 487 } })
    // fmtGhsNum(485) = "485" (GH/s range)
    expect(screen.getAllByText('485').length).toBeGreaterThanOrEqual(1)
  })

  it('renders SVG sparkline', () => {
    render(RollingRates, { props: { ghs1m: 1, ghs10m: 2, ghs1h: 3 } })
    expect(document.querySelector('svg.rolling-spark')).not.toBeNull()
  })

  it('hides error row when showErr=false', () => {
    render(RollingRates, { props: { ghs1m: 1, err1m: 0.5, showErr: false } })
    expect(document.querySelector('.rolling-row.err')).toBeNull()
  })

  it('shows error row when showErr=true (default)', () => {
    render(RollingRates, { props: { ghs1m: 1, err1m: 0.5 } })
    expect(document.querySelector('.rolling-row.err')).not.toBeNull()
  })

  it('renders generic values via values prop', () => {
    render(RollingRates, { props: { values: [12.5, 11.0, 10.2], unit: 'J/TH', decimals: 1 } })
    expect(screen.getAllByText('J/TH').length).toBeGreaterThanOrEqual(1)
    expect(screen.getAllByText('12.5').length).toBeGreaterThanOrEqual(1)
  })

  it('renders dash in generic mode for null entries', () => {
    render(RollingRates, { props: { values: [null, null, null] } })
    const dashes = screen.getAllByText('—')
    expect(dashes.length).toBeGreaterThanOrEqual(3)
  })

  it('suppresses unit span when unit is empty string in generic mode', () => {
    const { container } = render(RollingRates, { props: { values: [5.0, 4.5, 4.0], unit: '' } })
    // No unit spans inside the generic rolling-row
    const cells = container.querySelectorAll('.rolling-row .cell .u')
    expect(cells.length).toBe(0)
  })

  it('renders generic values with zero-or-negative as dash', () => {
    render(RollingRates, { props: { values: [0, -1, null] } })
    const dashes = screen.getAllByText('—')
    expect(dashes.length).toBeGreaterThanOrEqual(3)
  })

  it('hides sparkline when showSpark=false', () => {
    render(RollingRates, { props: { ghs1m: 1, ghs10m: 2, ghs1h: 3, showSpark: false } })
    expect(document.querySelector('svg.rolling-spark')).toBeNull()
  })
})
