import { describe, it, expect } from 'vitest'
import { render } from '@testing-library/svelte'
import { createRawSnippet } from 'svelte'
import InfoRow from './InfoRow.svelte'

describe('InfoRow', () => {
  it('renders label in dt and children in dd', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>VALUE</span>` }))
    const { container } = render(InfoRow, { props: { label: 'Foo', children } })
    expect(container.querySelector('dt')?.textContent).toBe('Foo')
    expect(container.querySelector('dd')?.textContent).toContain('VALUE')
  })

  it('adds .mono class on dd when mono=true', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>abc</span>` }))
    const { container } = render(InfoRow, { props: { label: 'Hash', mono: true, children } })
    expect(container.querySelector('dd')?.classList.contains('mono')).toBe(true)
  })

  it('does not add .mono class when mono=false (default)', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>abc</span>` }))
    const { container } = render(InfoRow, { props: { label: 'Hash', children } })
    expect(container.querySelector('dd')?.classList.contains('mono')).toBe(false)
  })

  it('adds .bad class on dd when bad=true', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>mismatch</span>` }))
    const { container } = render(InfoRow, { props: { label: 'Payout', bad: true, children } })
    expect(container.querySelector('dd')?.classList.contains('bad')).toBe(true)
  })

  it('does not add .bad class when bad=false (default)', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>ok</span>` }))
    const { container } = render(InfoRow, { props: { label: 'Payout', children } })
    expect(container.querySelector('dd')?.classList.contains('bad')).toBe(false)
  })

  it('sets title attribute on dd when title is provided', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>truncated</span>` }))
    const { container } = render(InfoRow, { props: { label: 'Addr', title: 'full address text', children } })
    expect(container.querySelector('dd')?.getAttribute('title')).toBe('full address text')
  })

  it('does not set title attribute when title is absent', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>val</span>` }))
    const { container } = render(InfoRow, { props: { label: 'Addr', children } })
    expect(container.querySelector('dd')?.hasAttribute('title')).toBe(false)
  })
})
