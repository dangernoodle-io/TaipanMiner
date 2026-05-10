import { test, expect } from '@playwright/test'
import { mockMinerApi } from './fixtures/api'

test.describe('History page', () => {
  test('renders without crashing', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/history')

    // History page mounts on the dashboard route bus; just make sure
    // navigation hash is honored and nothing throws.
    // It includes a window-selector toolbar with labels like 5m/15m/1h/all.
    await expect(page.locator('.sticky-pool, .pool-strip, button').first()).toBeVisible()
  })

  test('clicking a window selector button activates it', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/history')

    const buttons = page.locator('.windows button.win-btn')
    await expect(buttons.first()).toBeVisible()

    // Click the first window button and confirm it gets the .active class
    await buttons.first().click()
    await expect(buttons.first()).toHaveClass(/active/)
  })
})
