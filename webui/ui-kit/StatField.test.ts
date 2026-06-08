import { describe, it, expect } from 'vitest'
import { render } from '@testing-library/svelte'
import { createRawSnippet } from 'svelte'
import StatField from './StatField.svelte'

describe('StatField', () => {
  it('renders label in .sk and children in .sv', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>123 TH/s</span>` }))
    const { container } = render(StatField, { props: { label: 'Hashrate', children } })
    expect(container.querySelector('.sk')?.textContent).toBe('Hashrate')
    expect(container.querySelector('.sv')?.textContent).toContain('123 TH/s')
  })

  it('adds .value-top class on .stat-field when valueTop=true', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>val</span>` }))
    const { container } = render(StatField, { props: { label: 'Diff', valueTop: true, children } })
    expect(container.querySelector('.stat-field')?.classList.contains('value-top')).toBe(true)
  })

  it('does not add .value-top class when valueTop=false (default)', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>val</span>` }))
    const { container } = render(StatField, { props: { label: 'Diff', children } })
    expect(container.querySelector('.stat-field')?.classList.contains('value-top')).toBe(false)
  })

  it('adds .prominent class on .sv when prominent=true', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>big</span>` }))
    const { container } = render(StatField, { props: { label: 'Rate', prominent: true, children } })
    expect(container.querySelector('.sv')?.classList.contains('prominent')).toBe(true)
  })

  it('does not add .prominent class when prominent=false (default)', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>val</span>` }))
    const { container } = render(StatField, { props: { label: 'Rate', children } })
    expect(container.querySelector('.sv')?.classList.contains('prominent')).toBe(false)
  })

  it('adds .accent class on .sv when accent=true', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>highlight</span>` }))
    const { container } = render(StatField, { props: { label: 'Best', accent: true, children } })
    expect(container.querySelector('.sv')?.classList.contains('accent')).toBe(true)
  })

  it('does not add .accent class when accent=false (default)', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>val</span>` }))
    const { container } = render(StatField, { props: { label: 'Best', children } })
    expect(container.querySelector('.sv')?.classList.contains('accent')).toBe(false)
  })

  it('adds .mono class on .sv when mono=true', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>0xdeadbeef</span>` }))
    const { container } = render(StatField, { props: { label: 'Version', mono: true, children } })
    expect(container.querySelector('.sv')?.classList.contains('mono')).toBe(true)
  })

  it('does not add .mono class when mono=false (default)', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>val</span>` }))
    const { container } = render(StatField, { props: { label: 'Version', children } })
    expect(container.querySelector('.sv')?.classList.contains('mono')).toBe(false)
  })

  it('sets title attribute on .sv when title is provided', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>abc…xyz</span>` }))
    const { container } = render(StatField, { props: { label: 'Prev Block', title: 'full hash value', children } })
    expect(container.querySelector('.sv')?.getAttribute('title')).toBe('full hash value')
  })

  it('does not set title attribute when title is absent', () => {
    const children = createRawSnippet(() => ({ render: () => `<span>val</span>` }))
    const { container } = render(StatField, { props: { label: 'Field', children } })
    expect(container.querySelector('.sv')?.hasAttribute('title')).toBe(false)
  })
})
