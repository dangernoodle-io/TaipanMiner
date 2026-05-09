import { describe, it, expect } from 'vitest'
import { render, fireEvent } from '@testing-library/svelte'
import Toggle from './Toggle.svelte'

describe('Toggle', () => {
  it('renders an unchecked checkbox when checked=false', () => {
    render(Toggle, { props: { checked: false } })
    const input = document.querySelector('input[type="checkbox"]') as HTMLInputElement
    expect(input.checked).toBe(false)
  })

  it('renders a checked checkbox when checked=true', () => {
    render(Toggle, { props: { checked: true } })
    const input = document.querySelector('input[type="checkbox"]') as HTMLInputElement
    expect(input.checked).toBe(true)
  })

  it('applies disabled attribute when disabled=true', () => {
    render(Toggle, { props: { checked: false, disabled: true } })
    const input = document.querySelector('input[type="checkbox"]') as HTMLInputElement
    expect(input.disabled).toBe(true)
    expect(document.querySelector('.toggle.disabled')).not.toBeNull()
  })

  it('applies sm class when size=sm', () => {
    render(Toggle, { props: { checked: false, size: 'sm' } })
    expect(document.querySelector('.toggle.sm')).not.toBeNull()
  })

  it('does not apply sm class when size=md (default)', () => {
    render(Toggle, { props: { checked: false, size: 'md' } })
    expect(document.querySelector('.toggle.sm')).toBeNull()
  })

  it('fires change event on click', async () => {
    render(Toggle, { props: { checked: false } })
    const input = document.querySelector('input[type="checkbox"]') as HTMLInputElement
    await fireEvent.click(input)
    expect(input.checked).toBe(true)
  })
})
