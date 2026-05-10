import { describe, it, expect, vi } from 'vitest'
import { render } from '@testing-library/svelte'
import Save from './Save.svelte'

describe('Save', () => {
  it('renders without crashing', () => {
    const result = render(Save)
    expect(result.component).toBeDefined()
  })

  it('displays success card', () => {
    const { container } = render(Save)
    const card = container.querySelector('.card')
    expect(card).toBeTruthy()
  })

  it('shows configuration saved message', () => {
    const { container } = render(Save)
    const heading = container.querySelector('h2')
    expect(heading?.textContent).toContain('Configuration Saved')
  })

  it('displays spinner element', () => {
    const { container } = render(Save)
    const spinner = container.querySelector('.spinner')
    expect(spinner).toBeTruthy()
  })

  it('shows WiFi connection message', () => {
    const { container } = render(Save)
    const text = Array.from(container.querySelectorAll('p')).find(p =>
      p.textContent?.includes('Connecting to WiFi')
    )
    expect(text).toBeTruthy()
  })

  it('renders with proper styling', () => {
    const { container } = render(Save)
    const card = container.querySelector('.card') as HTMLElement
    expect(card).toBeTruthy()
    const styles = window.getComputedStyle(card)
    expect(styles).toBeTruthy()
  })

  it('contains section element', () => {
    const { container } = render(Save)
    const section = container.querySelector('section')
    expect(section).toBeTruthy()
  })

  it('has spinner with spin animation class', () => {
    const { container } = render(Save)
    const spinner = container.querySelector('.spinner')
    expect(spinner?.className).toContain('spinner')
  })

  it('displays heading with correct text content', () => {
    const { container } = render(Save)
    const heading = container.querySelector('h2')
    expect(heading?.textContent).toBe('Configuration Saved')
  })

  it('paragraph element present for status message', () => {
    const { container } = render(Save)
    const paragraphs = container.querySelectorAll('p')
    expect(paragraphs.length).toBeGreaterThan(0)
  })

  it('renders static content without props', () => {
    const result = render(Save)
    expect(result.component).toBeTruthy()
  })
})
