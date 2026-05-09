import { test, expect } from '@playwright/test'
import { mockMinerApi } from './fixtures/api'

test.describe('Knot page', () => {
  test('renders Knot heading', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/knot')
    await expect(page.getByRole('heading', { name: 'Knot' })).toBeVisible()
  })

  test('shows empty state when no peers', async ({ page }) => {
    await mockMinerApi(page) // knotFixture is []
    await page.goto('/#/knot')

    // Page should mount but render no peer rows
    await expect(page.locator('.peer-row, .peer')).toHaveCount(0)
  })

  test('renders peer rows when /api/knot returns peers', async ({ page }) => {
    await mockMinerApi(page, {
      overrides: {
        '/api/knot': [
          {
            instance: 'taipanminer-2',
            hostname: 'taipanminer-2',
            ip: '192.0.2.20',
            worker: 'miner-2',
            board: 'bitaxe-403',
            version: '1.2.2',
            state: 'mining',
            seen_ago_s: 5,
          },
        ],
      },
    })
    await page.goto('/#/knot')

    // The peer's hostname appears somewhere on the page
    await expect(page.getByText('taipanminer-2').first()).toBeVisible()
  })
})
