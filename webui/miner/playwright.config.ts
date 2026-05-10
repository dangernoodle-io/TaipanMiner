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
  fullyParallel: true,
  forbidOnly: !!process.env.CI,
  retries: process.env.CI ? 2 : 0,
  workers: process.env.CI ? 1 : undefined,
  reporter: process.env.CI ? [['github'], ['list']] : 'list',
  use: {
    baseURL: 'http://127.0.0.1:5173',
    trace: 'on-first-retry',
  },
  projects: [
    {
      name: 'chromium',
      use: {
        ...devices['Desktop Chrome'],
        channel: 'chromium',
        executablePath: '/usr/bin/chromium-browser',
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
