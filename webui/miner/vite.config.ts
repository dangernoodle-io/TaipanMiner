import { defineConfig, loadEnv } from 'vite'
import { svelte } from '@sveltejs/vite-plugin-svelte'

export default defineConfig(({ mode }) => {
  const env = loadEnv(mode, process.cwd(), 'VITE_')
  const target = env.VITE_MINER_URL

  if (!target) {
    console.warn('[vite] VITE_MINER_URL not set — /api proxy disabled. Copy .env.development.example to .env.development to enable.')
  }

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
      cssMinify: 'lightningcss',
      rollupOptions: {
        output: {
          entryFileNames: 'assets/index.js',
          chunkFileNames: 'assets/index.js',
          assetFileNames: (info) => {
            const name = info.name ?? ''
            if (name.endsWith('.css')) return 'assets/index.css'
            return 'assets/[name][extname]'
          },
        },
      },
    },
    server: {
      port: 5173,
      host: true,
      ...(target ? {
        proxy: {
          '/api': { target, changeOrigin: true },
          '/favicon.ico': { target, changeOrigin: true }
        }
      } : {})
    }
  }
})
