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
      cssCodeSplit: false,
      rollupOptions: {
        output: {
          entryFileNames: 'assets/index.js',
          chunkFileNames: (chunkInfo) => {
            // Stable names for lazy page chunks; shared chunks get stable names
            const pageNames = ['Pool', 'Update', 'Diagnostics', 'Settings', 'History', 'Knot']
            const name = chunkInfo.name ?? ''
            if (pageNames.includes(name)) return `assets/${name}.js`
            if (name === 'runtime') return 'assets/runtime.js'
            if (name === 'vendor') return 'assets/vendor.js'
            // Remaining shared chunks (small cross-page utilities) get a stable prefix
            return 'assets/index.js'
          },
          assetFileNames: (info) => {
            const name = info.name ?? ''
            if (name.endsWith('.css')) return 'assets/index.css'
            return 'assets/[name][extname]'
          },
          manualChunks: (id) => {
            // Pin svelte runtime to a stable chunk separate from app code
            if (id.includes('node_modules/svelte')) {
              return 'runtime'
            }
            // Pin uplot to vendor (large, History-only)
            if (id.includes('node_modules/uplot')) {
              return 'vendor'
            }
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
