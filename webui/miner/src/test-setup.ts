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

// Mock matchMedia for uplot
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

// Mock EventSource for Diagnostics SSE
class MockEventSource {
  constructor(url: string) {}
  addEventListener() {}
  removeEventListener() {}
  close() {}
  onopen: null = null
  onerror: null = null
  onmessage: null = null
}

if (!globalThis.EventSource) {
  Object.defineProperty(globalThis, 'EventSource', {
    writable: true,
    value: MockEventSource,
  })
}

// Mock canvas 2D context for uplot and other chart libraries
const noop = () => {}
HTMLCanvasElement.prototype.getContext = function(this: HTMLCanvasElement, contextType: string) {
  if (contextType !== '2d') return null
  return {
    canvas: this,
    fillRect: noop, clearRect: noop, strokeRect: noop, rect: noop,
    beginPath: noop, closePath: noop, moveTo: noop, lineTo: noop, arc: noop,
    fill: noop, stroke: noop, save: noop, restore: noop, translate: noop,
    scale: noop, rotate: noop, transform: noop, setTransform: noop, resetTransform: noop,
    drawImage: noop, putImageData: noop, getImageData: () => ({ data: new Uint8ClampedArray(4) }),
    createImageData: () => ({ data: new Uint8ClampedArray(4) }),
    measureText: () => ({ width: 0 }),
    fillText: noop, strokeText: noop,
    setLineDash: noop, getLineDash: () => [],
    createLinearGradient: () => ({ addColorStop: noop }),
    createRadialGradient: () => ({ addColorStop: noop }),
    createPattern: () => null,
    clip: noop, isPointInPath: () => false, isPointInStroke: () => false,
    quadraticCurveTo: noop, bezierCurveTo: noop, arcTo: noop, ellipse: noop,
    lineWidth: 1, lineCap: 'butt', lineJoin: 'miter', miterLimit: 10,
    lineDashOffset: 0, font: '10px sans-serif', textAlign: 'start', textBaseline: 'alphabetic',
    direction: 'inherit', fillStyle: '#000', strokeStyle: '#000',
    shadowBlur: 0, shadowColor: 'rgba(0,0,0,0)', shadowOffsetX: 0, shadowOffsetY: 0,
    globalAlpha: 1, globalCompositeOperation: 'source-over',
    imageSmoothingEnabled: true, imageSmoothingQuality: 'low',
  } as unknown as CanvasRenderingContext2D
} as typeof HTMLCanvasElement.prototype.getContext

// Mock Path2D for uplot
class MockPath2D {
  addPath() {}
  closePath() {}
  moveTo() {}
  lineTo() {}
  arc() {}
  arcTo() {}
  bezierCurveTo() {}
  ellipse() {}
  quadraticCurveTo() {}
  rect() {}
}

if (!globalThis.Path2D) {
  Object.defineProperty(globalThis, 'Path2D', {
    writable: true,
    value: MockPath2D,
  })
}
