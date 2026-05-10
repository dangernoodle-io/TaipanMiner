import { describe, it, expect } from 'vitest'
import { render } from '@testing-library/svelte'
import Brand from './Brand.svelte'

describe('Brand', () => {
  it('renders without crashing', () => {
    const { container } = render(Brand, { props: { title: 'Test Brand' } })
    expect(container).toBeInTheDocument()
  })

  it('displays brand title when passed via prop', () => {
    const { getByText } = render(Brand, { props: { title: 'My App' } })
    expect(getByText('My App')).toBeInTheDocument()
  })

  it('renders title in h1 element', () => {
    const { getByRole } = render(Brand, { props: { title: 'TaipanMiner' } })
    const heading = getByRole('heading', { level: 1 })
    expect(heading).toHaveTextContent('TaipanMiner')
  })

  it('has brand container with expected structure', () => {
    const { container } = render(Brand, { props: { title: 'Test' } })
    const brandHeader = container.querySelector('.brand')
    expect(brandHeader).toBeInTheDocument()
    expect(brandHeader).toHaveClass('brand')
  })

  it('renders logo div', () => {
    const { container } = render(Brand, { props: { title: 'Test' } })
    const logo = container.querySelector('.logo')
    expect(logo).toBeInTheDocument()
  })

  it('renders title section', () => {
    const { container } = render(Brand, { props: { title: 'Test' } })
    const titleSection = container.querySelector('.title')
    expect(titleSection).toBeInTheDocument()
  })
})
