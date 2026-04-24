import { defineConfig, loadEnv } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), 'VITE_')
  const target = env.VITE_MINER_URL || 'http://bitaxe-403-1.local'

  return {
    plugins: [svelte()],
    server: {
      port: 5173,
      host: true,
      proxy: {
        '/api': { target, changeOrigin: true },
        '/favicon.ico': { target, changeOrigin: true }
      }
    }
  }
})
