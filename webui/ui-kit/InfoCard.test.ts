import { describe, it, expect } from 'vitest'
import { render } from '@testing-library/svelte'
import { createRawSnippet } from 'svelte'
import InfoCard from './InfoCard.svelte'

describe('InfoCard', () => {
  it('renders the title as an h3', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>value</span>` }))
    const { container } = render(InfoCard, { props: { title: 'Device Info', children } })
    const h3 = container.querySelector('h3')
    expect(h3).not.toBeNull()
    expect(h3?.textContent).toBe('Device Info')
  })

  it('renders children inside the dl', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>row-content</span>` }))
    const { container } = render(InfoCard, { props: { title: 'Card', children } })
    const dl = container.querySelector('dl')
    expect(dl).not.toBeNull()
    expect(dl?.textContent).toContain('row-content')
  })

  it('wraps content in section.card', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>x</span>` }))
    const { container } = render(InfoCard, { props: { title: 'Test', children } })
    expect(container.querySelector('section.card')).not.toBeNull()
  })
})
