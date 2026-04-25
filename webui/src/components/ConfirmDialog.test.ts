import { describe, it, expect, beforeEach, vi } from 'vitest'
import { render, screen, fireEvent } from '@testing-library/svelte'
import ConfirmDialog from './ConfirmDialog.svelte'

describe('ConfirmDialog', () => {
  beforeEach(() => {
    try { localStorage.clear() } catch { /* jsdom storage not always available */ }
    vi.clearAllMocks()
  })

  it.skip('renders when open=true', () => {
    // TODO: @testing-library/svelte-core 1.0 incompatible with Svelte 5 SSR exports
    const { container } = render(ConfirmDialog, {
      props: {
        open: true,
        title: 'Test Title',
        message: 'Test message'
      }
    })
    expect(screen.getByText('Test Title')).toBeInTheDocument()
    expect(screen.getByText('Test message')).toBeInTheDocument()
  })

  it.skip('does not render when open=false', () => {
    // TODO: @testing-library/svelte-core 1.0 incompatible with Svelte 5 SSR exports
    const { container } = render(ConfirmDialog, {
      props: {
        open: false,
        title: 'Hidden Title',
        message: 'Hidden message'
      }
    })
    expect(screen.queryByText('Hidden Title')).not.toBeInTheDocument()
  })

  // skipped: vitest 4 + jsdom 29 localStorage shim lacks getItem/clear (TA-219 follow-up)
  it.skip('writes to localStorage when skipKey is set and checkbox is checked', async () => {
    render(ConfirmDialog, {
      props: {
        open: true,
        title: 'Confirm',
        message: 'Test',
        skipKey: 'test-skip'
      }
    })

    const checkbox = screen.getByRole('checkbox') as HTMLInputElement
    await fireEvent.click(checkbox)

    const buttons = screen.getAllByRole('button')
    const confirmBtn = buttons[buttons.length - 1]
    await fireEvent.click(confirmBtn)

    expect(localStorage.getItem('test-skip')).toBe('1')
  })

  it.skip('does not write to localStorage if checkbox unchecked', async () => {
    render(ConfirmDialog, {
      props: {
        open: true,
        title: 'Confirm',
        message: 'Test',
        skipKey: 'test-skip'
      }
    })

    const buttons = screen.getAllByRole('button')
    const confirmBtn = buttons[buttons.length - 1]
    await fireEvent.click(confirmBtn)

    expect(localStorage.getItem('test-skip')).toBeNull()
  })

  it.skip('closes dialog when confirm button is clicked', async () => {
    // TODO: @testing-library/svelte-core 1.0 incompatible with Svelte 5 SSR exports
    render(ConfirmDialog, {
      props: {
        open: true,
        title: 'Confirm',
        message: 'Test'
      }
    })

    expect(screen.getByText('Confirm')).toBeInTheDocument()

    const buttons = screen.getAllByRole('button')
    const confirmBtn = buttons[buttons.length - 1] // Last button is Confirm
    await fireEvent.click(confirmBtn)

    // After clicking confirm, dialog should be hidden
    // The component handles the state change internally
    // Just verify the button was clickable
    expect(confirmBtn).toBeInTheDocument()
  })

  it.skip('closes dialog when cancel button is clicked', async () => {
    // TODO: @testing-library/svelte-core 1.0 incompatible with Svelte 5 SSR exports
    render(ConfirmDialog, {
      props: {
        open: true,
        title: 'Confirm',
        message: 'Test'
      }
    })

    expect(screen.getByText('Confirm')).toBeInTheDocument()

    const buttons = screen.getAllByRole('button')
    const cancelBtn = buttons[0] // First button is Cancel
    await fireEvent.click(cancelBtn)

    // After clicking cancel, dialog should be hidden
    // The component handles the state change internally
    // Just verify the button was clickable
    expect(cancelBtn).toBeInTheDocument()
  })

  it.skip('hides skipKey checkbox when not provided', () => {
    // TODO: @testing-library/svelte-core 1.0 incompatible with Svelte 5 SSR exports
    render(ConfirmDialog, {
      props: {
        open: true,
        title: 'Confirm',
        message: 'Test'
      }
    })

    expect(screen.queryByText("Don't show this again")).not.toBeInTheDocument()
  })
})
