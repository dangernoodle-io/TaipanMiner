import { describe, it, expect, vi, beforeEach } from 'vitest'

vi.mock('svelte', () => ({
  mount: vi.fn().mockReturnValue({})
}))

vi.mock('./App.svelte', () => ({
  default: {}
}))

vi.mock('ui-kit/theme.css', () => ({}))
vi.mock('ui-kit/utilities.css', () => ({}))

describe('main.ts', () => {
  beforeEach(() => {
    const existing = document.getElementById('app')
    if (!existing) {
      const el = document.createElement('div')
      el.id = 'app'
      document.body.appendChild(el)
    }
    vi.clearAllMocks()
  })

  it('calls mount with App and #app target', async () => {
    const { mount } = await import('svelte')
    await import('./main')
    expect(mount).toHaveBeenCalledTimes(1)
    const [Component, options] = (mount as ReturnType<typeof vi.fn>).mock.calls[0]
    expect(options.target).toBeTruthy()
    expect(options.target.id).toBe('app')
  })
})
