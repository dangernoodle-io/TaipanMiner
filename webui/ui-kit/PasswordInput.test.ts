import { describe, it, expect } from 'vitest'
import { render, fireEvent } from '@testing-library/svelte'
import PasswordInput from './PasswordInput.svelte'

describe('PasswordInput', () => {
  it('renders input element', () => {
    const { container } = render(PasswordInput, { props: { value: '' } })
    const input = container.querySelector('input')
    expect(input).toBeInTheDocument()
  })

  it('renders password input type by default', () => {
    const { container } = render(PasswordInput, { props: { value: '' } })
    const input = container.querySelector('input')
    expect(input).toHaveAttribute('type', 'password')
  })

  it('shows/hides password on toggle button click', async () => {
    const { getByRole, container } = render(PasswordInput, { props: { value: '' } })
    const toggleBtn = getByRole('button')
    const input = container.querySelector('input')!

    expect(input.getAttribute('type')).toBe('password')

    await fireEvent.click(toggleBtn)
    expect(input.getAttribute('type')).toBe('text')

    await fireEvent.click(toggleBtn)
    expect(input.getAttribute('type')).toBe('password')
  })

  it('updates value via two-way binding', async () => {
    const { container } = render(PasswordInput, { props: { value: '' } })
    const input = container.querySelector('input') as HTMLInputElement

    await fireEvent.input(input, { target: { value: 'test-password' } })
    expect(input.value).toBe('test-password')
  })

  it('respects disabled state', () => {
    const { getByRole, container } = render(PasswordInput, {
      props: { value: '', disabled: true },
    })
    const input = container.querySelector('input')
    const toggleBtn = getByRole('button')

    expect(input).toBeDisabled()
    expect(toggleBtn).toBeDisabled()
  })

  it('displays placeholder when provided', () => {
    const { container } = render(PasswordInput, {
      props: { value: '', placeholder: 'Enter password' },
    })
    const input = container.querySelector('input') as HTMLInputElement
    expect(input.placeholder).toBe('Enter password')
  })

  it('respects maxlength constraint', () => {
    const { container } = render(PasswordInput, {
      props: { value: '', maxlength: 10 },
    })
    const input = container.querySelector('input')
    expect(input).toHaveAttribute('maxlength', '10')
  })

  it('toggle button has aria-label that changes with visibility state', async () => {
    const { getByRole } = render(PasswordInput, { props: { value: '' } })
    const toggleBtn = getByRole('button')

    expect(toggleBtn).toHaveAttribute('aria-label', 'Show password')

    await fireEvent.click(toggleBtn)
    expect(toggleBtn).toHaveAttribute('aria-label', 'Hide password')
  })

  it('has pw-group container', () => {
    const { container } = render(PasswordInput, { props: { value: '' } })
    const group = container.querySelector('.pw-group')
    expect(group).toBeInTheDocument()
  })
})
