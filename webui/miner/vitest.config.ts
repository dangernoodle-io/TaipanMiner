import { defineConfig } from 'vitest/config'
import { svelte } from '@sveltejs/vite-plugin-svelte'

export default defineConfig({
  plugins: [svelte({ hot: false })],
  test: {
    environment: 'jsdom',
    globals: true,
    setupFiles: ['./src/test-setup.ts'],
    include: ['src/**/*.test.ts'],
    coverage: {
      provider: 'v8',
      reporter: ['text', 'lcov'],
      reportsDirectory: './coverage',
      include: ['src/**/*.{ts,svelte}'],
      exclude: [
        'src/**/*.test.ts',
        'src/test-setup.ts',
        // Phase B (PR #351 follow-up): component and page .svelte files are
        // excluded from coverage until the Svelte SSR mount issue is resolved.
        // @testing-library/svelte resolves the SSR bundle in vitest's jsdom
        // environment, causing `mount(...)` to throw lifecycle_function_unavailable.
        // See ConfirmDialog.test.ts skip comments for the full error.
        'src/components/**/*.svelte',
        'src/pages/**/*.svelte',
      ],
    },
  },
})
