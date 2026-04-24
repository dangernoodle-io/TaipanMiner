import { writable } from 'svelte/store'

export type Route = 'dashboard' | 'system' | 'pool' | 'settings' | 'history' | 'diagnostics' | 'update'

const defaultRoute: Route = 'dashboard'

function parseHash(): Route {
  const h = window.location.hash.replace(/^#\/?/, '')
  switch (h) {
    case 'system':
    case 'pool':
    case 'settings':
    case 'history':
    case 'diagnostics':
    case 'update':
      return h
    default:
      return defaultRoute
  }
}

export const route = writable<Route>(parseHash())

window.addEventListener('hashchange', () => route.set(parseHash()))

export function goto(r: Route) {
  window.location.hash = `/${r}`
}
