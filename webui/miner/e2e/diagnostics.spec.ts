import { test, expect } from './fixtures'
import { mockMinerApi } from './fixtures/api'

test.describe('Diagnostics page', () => {
  test('renders Recent telemetry drops and Device sections', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/diagnostics')

    await expect(page.getByRole('heading', { name: /Recent telemetry drops/ })).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Device' })).toBeVisible()
  })

  test('renders Live Logs heading', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/diagnostics')

    await expect(page.getByRole('heading', { name: /Live Logs/ })).toBeVisible()
  })

  test('shows empty state when no telemetry drops', async ({ page }) => {
    await mockMinerApi(page) // diagAsicFixture has empty recent_drops
    await page.goto('/#/diagnostics')

    await expect(page.getByText('No recent drops.')).toBeVisible()
  })

  test('renders telemetry drop rows when present', async ({ page }) => {
    await mockMinerApi(page, {
      overrides: {
        '/api/diag/asic': {
          recent_drops: [
            { ts_ago_s: 30, chip: 0, kind: 'total', domain: 0, addr: 0, ghs: 100, delta: -20, elapsed_s: 1 },
            { ts_ago_s: 90, chip: 0, kind: 'domain', domain: 2, addr: 0, ghs: 25, delta: -5, elapsed_s: 1 },
          ],
        },
      },
    })
    await page.goto('/#/diagnostics')

    // Two rows in the drops table tbody
    await expect(page.locator('table.drops tbody tr')).toHaveCount(2)
  })
})
