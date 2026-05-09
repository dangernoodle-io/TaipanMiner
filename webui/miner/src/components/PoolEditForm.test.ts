import { describe, it, expect, vi } from 'vitest'
import { render, screen, fireEvent } from '@testing-library/svelte'
import PoolEditForm from './PoolEditForm.svelte'

function makeForm() {
  return {
    host: 'pool.example.com',
    port: 3333,
    wallet: 'bc1qtest0000',
    worker: 'miner-1',
    pool_pass: 'x',
    extranonce_subscribe: true,
    decode_coinbase: true
  }
}

describe('PoolEditForm', () => {
  it('renders the kind heading', () => {
    render(PoolEditForm, { props: { form: makeForm(), kind: 'Primary' } })
    expect(screen.getByText('Primary pool')).toBeInTheDocument()
  })

  it('renders Fallback heading for fallback kind', () => {
    render(PoolEditForm, { props: { form: makeForm(), kind: 'Fallback' } })
    expect(screen.getByText('Fallback pool')).toBeInTheDocument()
  })

  it('renders form fields with values', () => {
    render(PoolEditForm, { props: { form: makeForm(), kind: 'Primary' } })
    const hostInput = document.getElementById('pool-host') as HTMLInputElement
    expect(hostInput.value).toBe('pool.example.com')
  })

  it('renders port input with value', () => {
    render(PoolEditForm, { props: { form: makeForm(), kind: 'Primary' } })
    const portInput = document.getElementById('pool-port') as HTMLInputElement
    expect(portInput.value).toBe('3333')
  })

  it('renders Save button', () => {
    render(PoolEditForm, { props: { form: makeForm(), kind: 'Primary' } })
    expect(screen.getByRole('button', { name: 'Save' })).toBeInTheDocument()
  })

  it('renders Cancel button', () => {
    render(PoolEditForm, { props: { form: makeForm(), kind: 'Primary' } })
    expect(screen.getByRole('button', { name: 'Cancel' })).toBeInTheDocument()
  })

  it('shows Saving… when saving=true', () => {
    render(PoolEditForm, { props: { form: makeForm(), kind: 'Primary', saving: true } })
    expect(screen.getByText('Saving…')).toBeInTheDocument()
  })

  it('disables inputs when saving=true', () => {
    render(PoolEditForm, { props: { form: makeForm(), kind: 'Primary', saving: true } })
    const hostInput = document.getElementById('pool-host') as HTMLInputElement
    expect(hostInput.disabled).toBe(true)
  })

  it('shows saveMsg when provided', () => {
    render(PoolEditForm, { props: { form: makeForm(), kind: 'Primary', saveMsg: 'Pool saved' } })
    expect(screen.getByText('Pool saved')).toBeInTheDocument()
  })

  it('renders Advanced Options section', () => {
    render(PoolEditForm, { props: { form: makeForm(), kind: 'Primary' } })
    expect(screen.getByText('Advanced Options')).toBeInTheDocument()
  })
})
