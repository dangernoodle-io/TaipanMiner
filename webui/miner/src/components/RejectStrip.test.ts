import { describe, it, expect } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import RejectStrip from './RejectStrip.svelte'
import type { RejectedBreakdown } from '../lib/api'

describe('RejectStrip', () => {
  it('renders nothing when rejected is undefined', () => {
    const { container } = render(RejectStrip, { props: { rejected: undefined } })
    expect(container.querySelector('.reject-strip')).toBeNull()
  })

  it('renders nothing when all counts are zero', () => {
    const rejected = { job_not_found: 0, low_difficulty: 0, duplicate: 0, stale_prevhash: 0, other: 0 }
    const { container } = render(RejectStrip, { props: { rejected: rejected as unknown as RejectedBreakdown } })
    expect(container.querySelector('.reject-strip')).toBeNull()
  })

  it('renders "Rejected" label when there are non-zero counts', () => {
    const rejected = { job_not_found: 3, low_difficulty: 0, duplicate: 0, stale_prevhash: 0, other: 0 }
    render(RejectStrip, { props: { rejected: rejected as unknown as RejectedBreakdown } })
    expect(screen.getByText('Rejected')).toBeInTheDocument()
  })

  it('renders job_not_found label', () => {
    const rejected = { job_not_found: 2, low_difficulty: 0, duplicate: 0, stale_prevhash: 0, other: 0 }
    render(RejectStrip, { props: { rejected: rejected as unknown as RejectedBreakdown } })
    expect(screen.getByText('job not found')).toBeInTheDocument()
    expect(screen.getByText('2')).toBeInTheDocument()
  })

  it('renders multiple reject reasons', () => {
    const rejected = { job_not_found: 1, low_difficulty: 2, duplicate: 0, stale_prevhash: 3, other: 0 }
    render(RejectStrip, { props: { rejected: rejected as unknown as RejectedBreakdown } })
    expect(screen.getByText('job not found')).toBeInTheDocument()
    expect(screen.getByText('low diff')).toBeInTheDocument()
    expect(screen.getByText('stale prevhash')).toBeInTheDocument()
  })

  it('has aria-label for accessibility', () => {
    const rejected = { job_not_found: 1, low_difficulty: 0, duplicate: 0, stale_prevhash: 0, other: 0 }
    render(RejectStrip, { props: { rejected: rejected as unknown as RejectedBreakdown } })
    expect(document.querySelector('[aria-label="Rejected share breakdown"]')).not.toBeNull()
  })
})
