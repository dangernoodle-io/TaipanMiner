import { describe, it, expect } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import UpdateBadge from './UpdateBadge.svelte'

describe('UpdateBadge', () => {
  it('renders nothing when available is false', () => {
    const { container } = render(UpdateBadge, { props: { available: false, latest: null } })
    expect(container.querySelector('.update-badge')).toBeNull()
  })

  it('renders with latest version when available', () => {
    render(UpdateBadge, { props: { available: true, latest: 'v1.2.3' } })
    expect(screen.getByText(/Update available.*v1\.2\.3/)).toBeInTheDocument()
  })

  it('renders without version suffix when latest is null but available is true', () => {
    render(UpdateBadge, { props: { available: true, latest: null } })
    const btn = screen.getByRole('button')
    expect(btn.textContent?.trim()).toBe('Update available')
  })
})
