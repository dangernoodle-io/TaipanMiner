import { defineConfig, devices } from '@playwright/test'

/**
 * Playwright config for the miner SPA.
 *
 * The dev server is launched by Playwright (via `webServer`) and the SPA's
 * /api/* calls are intercepted per-test using `page.route()` — no real miner
 * is required. This means CI runs the same way as local: `pnpm e2e`.
 */
export default defineConfig({
  testDir: './e2e',
  testMatch: '*.spec.ts',
  fullyParallel: !process.env.CI,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 2 : 0,
  workers: process.env.CI ? 1 : undefined,
  reporter: process.env.CI
    ? [
        ['github'],
        ['list'],
        [
          'monocart-reporter',
          {
            name: 'TaipanMiner E2E',
            outputFile: './test-results/report.html',
            coverage: {
              lcov: true,
              outputDir: './coverage-e2e',
              reports: ['lcovonly'],
              entryFilter: (entry) => {
                const url = entry.url || ''
                if (url.includes('/node_modules/')) return false
                if (url.includes('/@fs/')) return false
                if (url.includes('/ui-kit/')) return false
                if (url.includes('-svelte&type=style')) return false
                if (url.endsWith('.css')) return false
                if (url.endsWith('.svg') || url.endsWith('.png') || url.endsWith('.ico')) return false
                return true
              },
              sourceFilter: (sourcePath) => {
                if (sourcePath.includes('node_modules')) return false
                if (sourcePath.includes('ui-kit')) return false
                if (sourcePath.includes('-svelte&type=style')) return false
                if (sourcePath.endsWith('.css')) return false
                return /(^|\/)src\//.test(sourcePath)
              },
              sourcePath: (filePath) => {
                let p = filePath.replace(/^127\.0\.0\.1-5173\//, '')
                p = p.replace(/^@fs\/.*?\/webui\/miner\//, '')
                p = p.replace(/^\//, '')
                const idx = p.indexOf('src/')
                if (idx > 0) p = p.slice(idx)
                return p
              },
            },
          },
        ],
      ]
    : 'list',
  use: {
    baseURL: 'http://127.0.0.1:5173',
    trace: 'on-first-retry',
  },
  projects: [
    {
      name: 'chromium',
      use: {
        ...devices['Desktop Chrome'],
        channel: 'chrome',
      },
    },
  ],
  webServer: {
    command: 'pnpm run dev -- --host 127.0.0.1 --port 5173',
    url: 'http://127.0.0.1:5173',
    reuseExistingServer: !process.env.CI,
    stdout: 'ignore',
    stderr: 'pipe',
    timeout: 60_000,
  },
})
