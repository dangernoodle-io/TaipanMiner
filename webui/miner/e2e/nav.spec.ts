import { test, expect } from './fixtures'
import { mockMinerApi } from './fixtures/api'

test.describe('Navigation — Knot link visibility', () => {
  test('renders Knot link in nav when health.network.knot=true', async ({ page }) => {
    await mockMinerApi(page, {
      overrides: {
        '/api/health': {
          ok: true,
          free_heap: 180000,
          validated: true,
          network: { connected: true, rssi: -50, disc_age_s: 0, retry_count: 0, mdns: 'taipan.local', knot: true },
        },
      },
    })
    await page.goto('/#/dashboard')

    const knotLink = page.locator('a[href="#/knot"]')
    await expect(knotLink).toBeVisible()
  })

  test('does NOT render Knot link in nav when health.network.knot=false', async ({ page }) => {
    await mockMinerApi(page, {
      overrides: {
        '/api/health': {
          ok: true,
          free_heap: 180000,
          validated: true,
          network: { connected: true, rssi: -50, disc_age_s: 0, retry_count: 0, mdns: 'taipan.local', knot: false },
        },
      },
    })
    await page.goto('/#/dashboard')

    const knotLink = page.locator('a[href="#/knot"]')
    await expect(knotLink).not.toBeVisible()
  })

  test('renders other nav links regardless of Knot status', async ({ page }) => {
    await mockMinerApi(page, {
      overrides: {
        '/api/health': {
          ok: true,
          free_heap: 180000,
          validated: true,
          network: { connected: true, rssi: -50, disc_age_s: 0, retry_count: 0, mdns: 'taipan.local', knot: false },
        },
      },
    })
    await page.goto('/#/dashboard')

    await expect(page.getByText('Dashboard')).toBeVisible()
    await expect(page.getByText('Settings')).toBeVisible()
    await expect(page.getByText('System')).toBeVisible()
    await expect(page.getByText('Pool')).toBeVisible()
  })
})
