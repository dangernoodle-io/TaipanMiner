import { test, expect } from './fixtures'
import { mockMinerApi, updateStatusFixture } from './fixtures/api'

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
    await mockMinerApi(page)

    // Stateful /api/update/status: kickOtaCheck reads it once for `before.last_check_ts`,
    // then polls until last_check_ts advances. The first read returns the pre-kick state;
    // every subsequent read returns the post-kick state with a bumped timestamp + the
    // available=true payload that drives the Install button.
    let calls = 0
    const before = { ...updateStatusFixture, last_check_ok: true }
    const after = {
      ...updateStatusFixture,
      available: true,
      latest: '1.3.0',
      download_url: 'https://example.com/firmware-1.3.0.bin',
      last_check_ts: updateStatusFixture.last_check_ts + 60,
    }
    await page.route('**/api/update/status', async (route) => {
      const body = calls++ === 0 ? before : after
      await route.fulfill({
        status: 200,
        contentType: 'application/json',
        body: JSON.stringify(body),
      })
    })
    await page.goto('/#/update')

    await page.getByRole('button', { name: /Check/i }).first().click()

    // After check, the primary "Install <version>" button appears in the
    // Firmware section. (The DEV mock-controls section also has an
    // "Install confirm dialog" button — match the version-specific label.)
    await expect(page.getByRole('button', { name: /Install 1\.3\.0/i })).toBeVisible({ timeout: 10_000 })
  })
})
