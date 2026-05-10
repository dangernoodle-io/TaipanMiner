import { describe, it, expect, vi, beforeEach } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import AlertBanner from './AlertBanner.svelte'
import type { Alert } from './AlertBanner.svelte'

describe('AlertBanner', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('renders nothing when alerts is empty', () => {
    const { container } = render(AlertBanner, { props: { alerts: [] } })
    expect(container.querySelector('.alert-banner')).toBeNull()
  })

  it('renders a single info alert', () => {
    const alerts: Alert[] = [{ key: 'a1', severity: 'info', message: 'Info message' }]
    render(AlertBanner, { props: { alerts } })
    expect(screen.getByText('Info message')).toBeInTheDocument()
    expect(document.querySelector('.banner.info')).not.toBeNull()
  })

  it('renders a warning alert', () => {
    const alerts: Alert[] = [{ key: 'w1', severity: 'warning', message: 'Warning message' }]
    render(AlertBanner, { props: { alerts } })
    expect(screen.getByText('Warning message')).toBeInTheDocument()
    expect(document.querySelector('.banner.warning')).not.toBeNull()
  })

  it('renders a danger alert', () => {
    const alerts: Alert[] = [{ key: 'd1', severity: 'danger', message: 'Danger message' }]
    render(AlertBanner, { props: { alerts } })
    expect(screen.getByText('Danger message')).toBeInTheDocument()
    expect(document.querySelector('.banner.danger')).not.toBeNull()
  })

  it('renders multiple alerts', () => {
    const alerts: Alert[] = [
      { key: 'a', severity: 'info', message: 'First' },
      { key: 'b', severity: 'warning', message: 'Second' },
      { key: 'c', severity: 'danger', message: 'Third' }
    ]
    render(AlertBanner, { props: { alerts } })
    expect(screen.getByText('First')).toBeInTheDocument()
    expect(screen.getByText('Second')).toBeInTheDocument()
    expect(screen.getByText('Third')).toBeInTheDocument()
  })
})
