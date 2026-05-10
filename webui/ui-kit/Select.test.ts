import { describe, it, expect } from 'vitest'
import { render, fireEvent } from '@testing-library/svelte'
import Select from './Select.svelte'

describe('Select', () => {
  const options = [
    { value: 'opt1', label: 'Option 1' },
    { value: 'opt2', label: 'Option 2' },
    { value: 'opt3', label: 'Option 3' },
  ]

  it('renders select element', () => {
    const { getByRole } = render(Select, {
      props: { value: '', options },
    })
    expect(getByRole('combobox')).toBeInTheDocument()
  })

  it('renders all provided options', () => {
    const { getByRole, getAllByRole } = render(Select, {
      props: { value: '', options },
    })
    const optionElements = getAllByRole('option')
    expect(optionElements.length).toBeGreaterThanOrEqual(3)
  })

  it('displays option labels correctly', () => {
    const { getByText } = render(Select, {
      props: { value: '', options },
    })
    expect(getByText('Option 1')).toBeInTheDocument()
    expect(getByText('Option 2')).toBeInTheDocument()
    expect(getByText('Option 3')).toBeInTheDocument()
  })

  it('updates value when selection changes', async () => {
    const { container, getAllByRole } = render(Select, {
      props: { value: '', options },
    })
    const select = container.querySelector('select') as HTMLSelectElement
    const optionElements = getAllByRole('option')
    const selectOpt2 = Array.from(optionElements).find(opt => opt.textContent?.includes('Option 2')) as HTMLOptionElement

    if (selectOpt2) {
      await fireEvent.change(select, { target: { value: selectOpt2.value } })
      expect(select.value).toBe('opt2')
    }
  })

  it('respects disabled state', () => {
    const { getByRole } = render(Select, {
      props: { value: '', options, disabled: true },
    })
    const select = getByRole('combobox') as HTMLSelectElement
    expect(select.disabled).toBe(true)
  })

  it('displays placeholder when provided and no value selected', () => {
    const { getByText } = render(Select, {
      props: { value: '', options, placeholder: 'Choose an option' },
    })
    expect(getByText('Choose an option')).toBeInTheDocument()
  })

  it('placeholder option is disabled', () => {
    const { getAllByRole } = render(Select, {
      props: { value: '', options, placeholder: 'Choose an option' },
    })
    const optionElements = getAllByRole('option')
    const placeholderOpt = Array.from(optionElements).find(opt =>
      opt.textContent?.includes('Choose an option'),
    ) as HTMLOptionElement
    expect(placeholderOpt.disabled).toBe(true)
  })

  it('supports string value type', async () => {
    const stringOptions = [
      { value: 'alice', label: 'Alice' },
      { value: 'bob', label: 'Bob' },
    ]
    const { container, getAllByRole } = render(Select, {
      props: { value: '', options: stringOptions },
    })
    const select = container.querySelector('select') as HTMLSelectElement
    const optionElements = getAllByRole('option')
    const selectBob = Array.from(optionElements).find(opt => opt.textContent?.includes('Bob')) as HTMLOptionElement

    if (selectBob) {
      await fireEvent.change(select, { target: { value: selectBob.value } })
      expect(select.value).toBe('bob')
    }
  })

  it('supports numeric value type', async () => {
    const numOptions = [
      { value: 1, label: 'First' },
      { value: 2, label: 'Second' },
    ]
    const { container, getAllByRole } = render(Select, {
      props: { value: '', options: numOptions },
    })
    const select = container.querySelector('select') as HTMLSelectElement
    const optionElements = getAllByRole('option')
    const selectSecond = Array.from(optionElements).find(opt => opt.textContent?.includes('Second')) as HTMLOptionElement

    if (selectSecond) {
      await fireEvent.change(select, { target: { value: selectSecond.value } })
      expect(select.value).toBe('2')
    }
  })
})
