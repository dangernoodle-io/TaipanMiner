import { test, expect } from '@playwright/test'
import { mockMinerApi, infoFixture } from './fixtures/api'

test.describe('Dashboard', () => {
  test('renders pool strip, hero, and stat cards on a healthy miner', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/dashboard')

    // Pool strip shows host:port and worker
    await expect(page.getByText('pool.example.com:3333')).toBeVisible()
    await expect(page.getByText('miner-1').first()).toBeVisible()

    // Hero hashrate (485.5 GH/s) is rendered — appears in both the hero
    // value and the chip rate; first() matches the hero.
    await expect(page.getByText('485.5 GH/s').first()).toBeVisible()

    // Heat / Fan / Power / Performance card headers are present (ASIC mode)
    await expect(page.getByRole('heading', { name: 'Heat' })).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Fan' })).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Power' })).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Performance' })).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Chips' })).toBeVisible()
  })

  test('hides ASIC-only cards on a tdongle miner', async ({ page }) => {
    // tdongle: /api/power returns 404 → hasAsic=false → ASIC cards hidden
    await mockMinerApi(page, {
      notFound: ['/api/power', '/api/fan'],
      overrides: {
        '/api/info': { ...infoFixture, board: 'tdongle-s3' },
      },
    })
    await page.goto('/#/dashboard')

    // Hero is still present
    await expect(page.getByText('485.5 GH/s').first()).toBeVisible()

    // ASIC cards should NOT be present
    await expect(page.getByRole('heading', { name: 'Heat' })).not.toBeVisible()
    await expect(page.getByRole('heading', { name: 'Fan' })).not.toBeVisible()
  })

  test('clicking the Fan card opens the FanEditDialog', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/dashboard')

    // Wait for ASIC probe
    await expect(page.getByRole('heading', { name: 'Fan' })).toBeVisible()

    // Click the Fan card — it has role=button via the section[role="button"]
    // wrapper; locate by title attribute since there's no accessible name.
    await page.locator('section[title="Edit fan settings"]').click()

    // Dialog appears
    await expect(page.getByRole('dialog')).toBeVisible()
    await expect(page.getByRole('button', { name: 'Save' })).toBeVisible()
  })

  test('shows disconnected banner when stats endpoint fails', async ({ page }) => {
    // First poll fails — connected store flips false after 2 fails
    await mockMinerApi(page, { statusOverrides: { '/api/stats': 503 } })
    await page.goto('/#/dashboard')

    // Eventually shows the disconnected alert (after 2 failed polls, ~10s)
    await expect(page.getByText('Miner unreachable')).toBeVisible({ timeout: 15_000 })
  })
})
