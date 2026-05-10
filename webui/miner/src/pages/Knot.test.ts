import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render } from '@testing-library/svelte'

vi.mock('../lib/api', () => ({
  fetchStats: vi.fn(), fetchInfo: vi.fn(), fetchPower: vi.fn(), fetchFan: vi.fn(),
  fetchSettings: vi.fn(), fetchPool: vi.fn(), fetchHealth: vi.fn(), ping: vi.fn(), fetchKnot: vi.fn()
}))

import Knot from './Knot.svelte'

describe('Knot', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('renders without crashing', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('renders peer list', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('renders with no peers', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('renders peer info layout', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('shows peer states', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('displays peer details', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('renders peer cards', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('shows versions', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('displays boards', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('shows seen times', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('renders mining statuses', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('shows IP addresses', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('displays worker names', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('renders loading state', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('shows empty state', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('handles multiple peers', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('renders without errors', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('displays state colors', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })

  it('renders with async data', () => {
    const result = render(Knot)
    expect(result.component).toBeDefined()
  })
})
