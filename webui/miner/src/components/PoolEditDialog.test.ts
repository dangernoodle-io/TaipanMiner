import { describe, it, expect } from 'vitest'
import { render, screen } from '@testing-library/svelte'
import PoolEditDialog from './PoolEditDialog.svelte'

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

describe('PoolEditDialog', () => {
  it('does not render when open=false', () => {
    const { container } = render(PoolEditDialog, {
      props: { open: false, form: makeForm(), kind: 'Primary' }
    })
    expect(container.querySelector('[role="dialog"]')).toBeNull()
  })

  it('renders dialog when open=true', () => {
    render(PoolEditDialog, {
      props: { open: true, form: makeForm(), kind: 'Primary' }
    })
    expect(document.querySelector('[role="dialog"]')).not.toBeNull()
  })

  it('renders the form heading', () => {
    render(PoolEditDialog, {
      props: { open: true, form: makeForm(), kind: 'Primary' }
    })
    expect(screen.getByText('Primary pool')).toBeInTheDocument()
  })

  it('renders Fallback heading for fallback kind', () => {
    render(PoolEditDialog, {
      props: { open: true, form: makeForm(), kind: 'Fallback' }
    })
    expect(screen.getByText('Fallback pool')).toBeInTheDocument()
  })

  it('has aria-modal=true', () => {
    render(PoolEditDialog, {
      props: { open: true, form: makeForm(), kind: 'Primary' }
    })
    expect(document.querySelector('[aria-modal="true"]')).not.toBeNull()
  })
})
