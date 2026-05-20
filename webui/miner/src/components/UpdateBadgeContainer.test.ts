import { describe, it, expect } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import UpdateBadgeContainer from './UpdateBadgeContainer.svelte'

describe('UpdateBadgeContainer', () => {
  it('passes updateState through to UpdateBadge — visible when available', () => {
    render(UpdateBadgeContainer, {
      props: { updateState: { available: true, latest: 'v9.9.9' } },
    })
    expect(screen.getByText(/Update available.*v9\.9\.9/)).toBeInTheDocument()
  })

  it('renders nothing when updateState.available is false', () => {
    const { container } = render(UpdateBadgeContainer, {
      props: { updateState: { available: false, latest: null } },
    })
    expect(container.querySelector('.update-badge')).toBeNull()
  })
})
