import { defineConfig, loadEnv } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), 'VITE_')
  const target = env.VITE_MINER_URL || 'http://bitaxe-403-1.local'

  return {
    plugins: [svelte()],
    build: {
      minify: 'terser',
      terserOptions: {
        compress: {
          passes: 3,
          drop_console: true,
          drop_debugger: true,
          pure_getters: true,
          unsafe_arrows: true,
          unsafe_methods: true
        },
        mangle: { toplevel: true },
        format: { comments: false }
      },
      cssMinify: 'lightningcss'
    },
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
