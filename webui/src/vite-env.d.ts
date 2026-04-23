/// <reference types="svelte" />
/// <reference types="vite/client" />

interface ImportMetaEnv {
  readonly VITE_MINER_URL: string
}

interface ImportMeta {
  readonly env: ImportMetaEnv
}
