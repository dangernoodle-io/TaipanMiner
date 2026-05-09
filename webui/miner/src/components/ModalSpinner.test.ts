import { describe, it, expect } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import ModalSpinner from './ModalSpinner.svelte'

describe('ModalSpinner', () => {
  it('does not render when visible=false (default)', () => {
    const { container } = render(ModalSpinner, { props: { visible: false } })
    expect(container.querySelector('.pool-backdrop')).toBeNull()
  })

  it('renders when visible=true', () => {
    render(ModalSpinner, { props: { visible: true, label: 'Loading' } })
    expect(screen.getByText('Loading')).toBeInTheDocument()
  })

  it('renders sublabel when provided', () => {
    render(ModalSpinner, { props: { visible: true, label: 'Saving', sublabel: 'Please wait' } })
    expect(screen.getByText('Please wait')).toBeInTheDocument()
  })

  it('does not render sublabel element when not provided', () => {
    render(ModalSpinner, { props: { visible: true, label: 'Saving', sublabel: '' } })
    expect(document.querySelector('.pool-panel p')).toBeNull()
  })

  it('has aria-live and aria-busy attributes', () => {
    render(ModalSpinner, { props: { visible: true, label: 'Working' } })
    const el = document.querySelector('[role="status"]')!
    expect(el.getAttribute('aria-live')).toBe('polite')
    expect(el.getAttribute('aria-busy')).toBe('true')
  })
})
