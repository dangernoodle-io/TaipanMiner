import '@testing-library/jest-dom/vitest'

function makeStorageShim(): Storage {
  const store = new Map<string, string>()
  return {
    get length() { return store.size },
    clear: () => store.clear(),
    getItem: (k: string) => store.get(k) ?? null,
    setItem: (k: string, v: string) => { store.set(k, String(v)) },
    removeItem: (k: string) => { store.delete(k) },
    key: (i: number) => Array.from(store.keys())[i] ?? null,
  }
}

Object.defineProperty(globalThis, 'localStorage', { value: makeStorageShim(), configurable: true, writable: true })
Object.defineProperty(globalThis, 'sessionStorage', { value: makeStorageShim(), configurable: true, writable: true })

if (!window.matchMedia) {
  Object.defineProperty(window, 'matchMedia', {
    writable: true,
    value: (query: string) => ({
      matches: false,
      media: query,
      onchange: null,
      addListener: () => {},
      removeListener: () => {},
      addEventListener: () => {},
      removeEventListener: () => {},
      dispatchEvent: () => {},
    }),
  })
}

;(globalThis as unknown as Record<string, unknown>).navigator =
  (globalThis as unknown as Record<string, unknown>).navigator || {}
;(globalThis as unknown as { navigator: Record<string, unknown> }).navigator.serial =
  (globalThis as unknown as { navigator: Record<string, unknown> }).navigator.serial || {
    requestPort: () => Promise.reject(new Error('no port (test)')),
    addEventListener: () => {},
    removeEventListener: () => {},
  }
