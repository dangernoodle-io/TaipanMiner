import { describe, it, expect, vi } from 'vitest'
import { render, screen, fireEvent } from '@testing-library/svelte'
import BlockFoundBanner from './BlockFoundBanner.svelte'

const baseFound = { host: 'pool.example.com', port: 3333 }

describe('BlockFoundBanner', () => {
  it('renders nothing when visible is false', () => {
    const { container } = render(BlockFoundBanner, {
      props: { state: { visible: false, lastFound: null, dismiss: vi.fn() } },
    })
    expect(container.querySelector('.block-found-banner')).toBeNull()
  })

  it('renders nothing when visible is true but lastFound is null', () => {
    const { container } = render(BlockFoundBanner, {
      props: { state: { visible: true, lastFound: null, dismiss: vi.fn() } },
    })
    expect(container.querySelector('.block-found-banner')).toBeNull()
  })

  it('renders banner with host:port when visible', () => {
    render(BlockFoundBanner, {
      props: { state: { visible: true, lastFound: { ...baseFound }, dismiss: vi.fn() } },
    })
    expect(screen.getByText(/Block found/)).toBeInTheDocument()
    expect(screen.getByText(/pool\.example\.com:3333/)).toBeInTheDocument()
  })

  it('calls dismiss when × button is clicked', async () => {
    const dismiss = vi.fn()
    render(BlockFoundBanner, {
      props: { state: { visible: true, lastFound: { ...baseFound }, dismiss } },
    })
    const btn = screen.getByRole('button', { name: /dismiss/i })
    await fireEvent.click(btn)
    expect(dismiss).toHaveBeenCalledOnce()
  })

  it('renders share_diff when provided', () => {
    render(BlockFoundBanner, {
      props: {
        state: {
          visible: true,
          lastFound: { ...baseFound, share_diff: 9876.54 },
          dismiss: vi.fn(),
        },
      },
    })
    expect(screen.getByText(/diff/)).toBeInTheDocument()
  })

  it('does not render diff section when share_diff is absent', () => {
    const { container } = render(BlockFoundBanner, {
      props: { state: { visible: true, lastFound: { ...baseFound }, dismiss: vi.fn() } },
    })
    expect(container.textContent).not.toMatch(/diff/)
  })
})
