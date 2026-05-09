import { test, expect } from '@playwright/test'
import { mockMinerApi, otaCheckFixture } from './fixtures/api'

test.describe('Update page', () => {
  test('renders Firmware and Manual Upload sections', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/update')

    await expect(page.getByRole('heading', { name: 'Firmware' })).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Manual Upload' })).toBeVisible()
  })

  test('Check button is enabled when miner is idle', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/update')

    const checkBtn = page.getByRole('button', { name: /Check/i }).first()
    await expect(checkBtn).toBeEnabled()
  })

  test('shows "Up to date" when OTA check returns no update available', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/update')

    await page.getByRole('button', { name: /Check/i }).first().click()

    // Result text shows the current version is up to date
    await expect(page.getByText(/Up to date|up-to-date|1\.2\.3/i).first()).toBeVisible({ timeout: 10_000 })
  })

  test('shows Install button when an update is available', async ({ page }) => {
    await mockMinerApi(page, {
      overrides: {
        '/api/ota/check': {
          ...otaCheckFixture,
          update_available: true,
          latest_version: '1.3.0',
        },
      },
    })
    await page.goto('/#/update')

    await page.getByRole('button', { name: /Check/i }).first().click()

    // After check, the primary "Install <version>" button appears in the
    // Firmware section. (The DEV mock-controls section also has an
    // "Install confirm dialog" button — match the version-specific label.)
    await expect(page.getByRole('button', { name: /Install 1\.3\.0/i })).toBeVisible({ timeout: 10_000 })
  })
})
