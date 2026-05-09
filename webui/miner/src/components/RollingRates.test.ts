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
})
