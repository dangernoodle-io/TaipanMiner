import { test, expect } from './fixtures'
import { mockMinerApi, infoFixture } from './fixtures/api'

test.describe('System page', () => {
  test.beforeEach(async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/system')
  })

  test('renders Device, Firmware, and Runtime cards', async ({ page }) => {
    await expect(page.getByRole('heading', { name: 'Device' })).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Firmware' })).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Runtime' })).toBeVisible()
  })

  test('shows board, MAC, IP from /api/info', async ({ page }) => {
    await expect(page.getByText('bitaxe-601').first()).toBeVisible()
    await expect(page.getByText('AA:BB:CC:DD:EE:FF')).toBeVisible()
    await expect(page.getByText('192.0.2.10')).toBeVisible()
  })

  test('shows version and IDF version from /api/info', async ({ page }) => {
    // Version 1.2.3 appears in the Header sub plus the Firmware card
    await expect(page.getByText('1.2.3').first()).toBeVisible()
    await expect(page.getByText('5.5.3')).toBeVisible()
  })

  test('renders RAM and Flash donuts', async ({ page }) => {
    await expect(page.getByText('RAM usage')).toBeVisible()
    await expect(page.getByText('Flash')).toBeVisible()
  })

  test('shows Uptime card', async ({ page }) => {
    // 7200s uptime → "2h 0m"
    await expect(page.getByText('Uptime')).toBeVisible()
    await expect(page.getByText('2h 0m')).toBeVisible()
  })
})

test.describe('System page · degraded states', () => {
  test('shows WiFi disconnected dot when health.network.connected=false', async ({ page }) => {
    await mockMinerApi(page, {
      overrides: {
        '/api/health': {
          ok: true,
          free_heap: 180000,
          validated: true,
          network: { connected: false, rssi: -90, disc_age_s: 5, retry_count: 3, mdns: null, stratum: false, stratum_fail_count: 7 },
        },
      },
    })
    await page.goto('/#/system')

    // The health row marks WiFi err — the data-state attribute reflects this
    await expect(page.locator('.h-row[data-state="err"]').first()).toBeVisible()
    // And the alert banner shows "Miner unreachable" only when stats fails;
    // here stratum=false → the stratum row is err
    await expect(page.locator('.h-row[data-state="err"]')).toHaveCount(2)
  })
})

test.describe('System page · Knot status', () => {
  test('renders Knot row with ok state when network.knot=true', async ({ page }) => {
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
    await page.goto('/#/system')

    const knotRow = page.locator('.h-row[data-state="ok"]').filter({ hasText: 'Knot' })
    await expect(knotRow).toBeVisible()
  })

  test('renders Knot row with idle state when network.knot=false', async ({ page }) => {
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
    await page.goto('/#/system')

    const knotRow = page.locator('.h-row[data-state="idle"]').filter({ hasText: 'Knot' })
    await expect(knotRow).toBeVisible()
  })

  test('renders Knot row with idle state when knot field is undefined', async ({ page }) => {
    await mockMinerApi(page, {
      overrides: {
        '/api/health': {
          ok: true,
          free_heap: 180000,
          validated: true,
          network: { connected: true, rssi: -50, disc_age_s: 0, retry_count: 0, mdns: 'taipan.local' },
        },
      },
    })
    await page.goto('/#/system')

    const knotRow = page.locator('.h-row[data-state="idle"]').filter({ hasText: 'Knot' })
    await expect(knotRow).toBeVisible()
  })
})
