import '@testing-library/jest-dom/vitest'

// Node 25+ exposes a built-in stub Web Storage API (see vitest-dev/vitest#8757).
// On Node 26+ that stub emits an ExperimentalWarning on every access unless
// --localstorage-file is passed (CLI-only, not allowed in NODE_OPTIONS).
// Even *probing* it (e.g. typeof globalThis.localStorage.getItem) trips the
// warning. So instead of feature-detecting, we unconditionally install our
// own in-memory shim — functionally equivalent to jsdom's storage and free
// of the Node experimental warning noise.
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
