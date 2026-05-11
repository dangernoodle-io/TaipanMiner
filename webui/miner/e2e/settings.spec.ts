import { test, expect } from './fixtures'
import { mockMinerApi, settingsFixture } from './fixtures/api'

test.describe('Settings page', () => {
  test('renders ASIC, Fan, General, Network sections on a bitaxe', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/settings')

    await expect(page.getByRole('heading', { name: /^ASIC/ })).toBeVisible()
    await expect(page.getByRole('heading', { name: /^Fan/ })).toBeVisible()
    await expect(page.getByRole('heading', { name: /^General/ })).toBeVisible()
    await expect(page.getByRole('heading', { name: /^Network/ })).toBeVisible()
  })

  test('clicking Fan Edit opens the FanEditDialog', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/settings')

    await page.getByRole('button', { name: 'Edit', exact: true }).click()
    await expect(page.getByRole('dialog')).toBeVisible()
  })

  test('toggling OLED display sends PATCH /api/settings', async ({ page }) => {
    let patched: unknown = null
    await mockMinerApi(page)
    // Override PATCH route after mockMinerApi (page.route is FIFO; this wins)
    await page.route('**/api/settings', async (route) => {
      if (route.request().method() === 'PATCH') {
        patched = JSON.parse(route.request().postData() ?? '{}')
        await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ status: 'ok', reboot_required: false }) })
        return
      }
      // GET falls through to fixture
      await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(settingsFixture) })
    })
    await page.goto('/#/settings')

    // Wait for load
    await expect(page.getByRole('heading', { name: /^General/ })).toBeVisible()

    // Find the OLED display row and toggle it. The actual <input> has
    // opacity:0 (custom Toggle slider), so click the label that wraps it.
    const oledRow = page.locator('.row').filter({ hasText: 'OLED display' })
    await oledRow.locator('label.toggle').click()

    // Status message confirms save
    await expect(oledRow.getByText('Saved')).toBeVisible()
    expect(patched).toMatchObject({ display_en: false })
  })

  test('shows error message when load fails', async ({ page }) => {
    await mockMinerApi(page, { statusOverrides: { '/api/settings': 500 } })
    await page.goto('/#/settings')

    await expect(page.getByText(/Failed to load settings/)).toBeVisible()
  })

  test('renders mDNS and Knot toggles in General section', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/settings')

    await expect(page.getByRole('heading', { name: /^General/ })).toBeVisible()
    const mdnsRow = page.locator('.row').filter({ hasText: 'mDNS' })
    const knotRow = page.locator('.row').filter({ hasText: 'Knot' })
    await expect(mdnsRow).toBeVisible()
    await expect(knotRow).toBeVisible()
  })

  test('Knot toggle is disabled when mDNS is off', async ({ page }) => {
    let patchedData: unknown = null
    await mockMinerApi(page, { overrides: { '/api/settings': { ...settingsFixture, mdns_en: false, knot_en: false } } })
    await page.route('**/api/settings', async (route) => {
      if (route.request().method() === 'PATCH') {
        patchedData = JSON.parse(route.request().postData() ?? '{}')
        await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ status: 'ok', reboot_required: false }) })
        return
      }
      await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ ...settingsFixture, mdns_en: false, knot_en: false }) })
    })
    await page.goto('/#/settings')

    await expect(page.getByRole('heading', { name: /^General/ })).toBeVisible()
    const knotRow = page.locator('.row').filter({ hasText: 'Knot' })
    const knotToggle = knotRow.locator('input[type="checkbox"]')
    await expect(knotToggle).toBeDisabled()
  })

  test('Knot toggle is enabled when mDNS is on', async ({ page }) => {
    await mockMinerApi(page, { overrides: { '/api/settings': { ...settingsFixture, mdns_en: true, knot_en: false } } })
    await page.goto('/#/settings')

    await expect(page.getByRole('heading', { name: /^General/ })).toBeVisible()
    const knotRow = page.locator('.row').filter({ hasText: 'Knot' })
    const knotToggle = knotRow.locator('input[type="checkbox"]')
    await expect(knotToggle).toBeEnabled()
  })

  test('toggling mDNS off sends PATCH and disables Knot', async ({ page }) => {
    let patchedData: unknown = null
    await mockMinerApi(page)
    await page.route('**/api/settings', async (route) => {
      if (route.request().method() === 'PATCH') {
        patchedData = JSON.parse(route.request().postData() ?? '{}')
        await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ status: 'ok', reboot_required: false }) })
        return
      }
      await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(settingsFixture) })
    })
    await page.goto('/#/settings')

    await expect(page.getByRole('heading', { name: /^General/ })).toBeVisible()

    const mdnsRow = page.locator('.row').filter({ hasText: 'mDNS' })
    await mdnsRow.locator('label.toggle').click()

    expect(patchedData).toMatchObject({ mdns_en: false })
    await expect(mdnsRow.getByText('Saved')).toBeVisible()
  })

  test('toggling Knot on sends PATCH with knot_en', async ({ page }) => {
    let patchedData: unknown = null
    await mockMinerApi(page, { overrides: { '/api/settings': { ...settingsFixture, mdns_en: true, knot_en: false } } })
    await page.route('**/api/settings', async (route) => {
      if (route.request().method() === 'PATCH') {
        patchedData = JSON.parse(route.request().postData() ?? '{}')
        await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ status: 'ok', reboot_required: false }) })
        return
      }
      await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify({ ...settingsFixture, mdns_en: true, knot_en: false }) })
    })
    await page.goto('/#/settings')

    await expect(page.getByRole('heading', { name: /^General/ })).toBeVisible()

    const knotRow = page.locator('.row').filter({ hasText: 'Knot' })
    await knotRow.locator('label.toggle').click()

    expect(patchedData).toMatchObject({ knot_en: true })
    await expect(knotRow.getByText('Saved')).toBeVisible()
  })
})
