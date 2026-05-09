import { describe, it, expect } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import Tooltip from './Tooltip.svelte'

describe('Tooltip', () => {
  it('renders tooltip text as aria-label', () => {
    render(Tooltip, { props: { text: 'Helper text' } })
    expect(screen.getByRole('button', { name: 'Helper text' })).toBeInTheDocument()
  })

  it('shows ? icon when icon=true', () => {
    render(Tooltip, { props: { text: 'Tip text', icon: true } })
    expect(document.querySelector('.ico')).not.toBeNull()
  })

  it('does not show icon element when icon=false (default)', () => {
    render(Tooltip, { props: { text: 'Tip text', icon: false } })
    expect(document.querySelector('.ico')).toBeNull()
  })

  it('renders tooltip text in the tip span', () => {
    render(Tooltip, { props: { text: 'Tooltip content', icon: true } })
    const tip = document.querySelector('.tip')
    expect(tip).not.toBeNull()
    expect(tip!.textContent).toBe('Tooltip content')
  })

  it('applies top class when placement=top', () => {
    render(Tooltip, { props: { text: 'Top tip', placement: 'top' } })
    expect(document.querySelector('.tip.top')).not.toBeNull()
  })

  it('does not apply top class when placement=bottom (default)', () => {
    render(Tooltip, { props: { text: 'Bottom tip', placement: 'bottom' } })
    expect(document.querySelector('.tip.top')).toBeNull()
  })
})
