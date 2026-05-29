// Single source of truth for rendering a firmware version string.
//
// The firmware version comes from `git describe --tags`, so tagged builds
// already carry their own `v` prefix (e.g. `v0.41.1`) while dev builds do not
// (e.g. `dev-20260529023337`). Display it verbatim — never prepend another `v`
// (that produced `vv0.41.1` in the prov footer). The leading-`v` collapse is a
// defensive guard so no caller can reintroduce the doubled prefix.
export function formatVersion(version?: string): string {
  const v = (version ?? '').trim()
  return v.replace(/^v+/, 'v')
}
