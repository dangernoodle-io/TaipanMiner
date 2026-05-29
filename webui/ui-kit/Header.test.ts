import { describe, it, expect } from 'vitest'
import { render } from '@testing-library/svelte'
import Header from './Header.svelte'

describe('Header', () => {
  it('renders the title', () => {
    const { getByRole } = render(Header, { props: { title: 'TaipanMiner' } })
    expect(getByRole('heading', { level: 1 })).toHaveTextContent('TaipanMiner')
  })

  it('renders the subtitle when provided', () => {
    const { container } = render(Header, {
      props: { title: 'TaipanMiner', subtitle: 'esp32-c3-supermini · v0.41.1' },
    })
    expect(container.querySelector('.sub')?.textContent).toBe('esp32-c3-supermini · v0.41.1')
  })

  it('omits the subtitle element when absent or empty', () => {
    const { container } = render(Header, { props: { title: 'TaipanMiner' } })
    expect(container.querySelector('.sub')).toBeNull()
  })
})
