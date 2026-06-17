import { test, expect } from './fixtures'
import { mockMinerApi, infoFixture, sensorsFixture } from './fixtures/api'

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

    // Performance / Tuning / Cooling / Fan / Chips card headers (ASIC mode)
    await expect(page.getByRole('heading', { name: 'Performance' })).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Tuning' })).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Cooling' })).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Fan' })).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Chips' })).toBeVisible()
  })

  test('hides ASIC-only cards on a tdongle miner', async ({ page }) => {
    // tdongle: capabilities has no 'asic' → hasAsic=false → ASIC cards hidden
    await mockMinerApi(page, {
      overrides: {
        '/api/info': { ...infoFixture, board: 'tdongle-s3', capabilities: ['ui'] },
        '/api/sensors': {
          ...sensorsFixture,
          power: { ...sensorsFixture.power, present: false },
        },
      },
    })
    await page.goto('/#/dashboard')

    // Hero is still present
    await expect(page.getByText('485.5 GH/s').first()).toBeVisible()

    // ASIC cards should NOT be present
    await expect(page.getByRole('heading', { name: 'Cooling' })).not.toBeVisible()
    await expect(page.getByRole('heading', { name: 'Fan' })).not.toBeVisible()
  })

  test('clicking the Fan card opens the FanEditDialog', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/dashboard')

    // Wait for ASIC probe
    await expect(page.getByRole('heading', { name: 'Fan' })).toBeVisible()

    // Click the Fan card's edit button (title attribute identifies it).
    await page.locator('button[title="Edit fan settings"]').click()

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

  // Regression net: B1-269 deleted /api/power|fan|thermal; dashboard must show
  // ASIC stat tiles when /api/sensors reports power.present=true + capabilities:['asic'].
  test('ASIC stat tiles visible when /api/sensors reports power.present=true', async ({ page }) => {
    await mockMinerApi(page, {
      overrides: {
        '/api/info': {
          ...infoFixture,
          capabilities: ['asic', 'fan', 'power', 'ui', 'knot'],
          mining: { asic: { model: 'BM1370', chips: 1, small_cores_per_chip: 894 } },
        },
        '/api/sensors': sensorsFixture,
      },
    })
    await page.goto('/#/dashboard')

    // ASIC cards present — this is the core regression: if hasAsic stays false
    // these headings never render and the dashboard collapses.
    await expect(page.getByRole('heading', { name: 'Performance' })).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Cooling' })).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Fan' })).toBeVisible()

    // StatTile labels confirm the power/thermal sections rendered
    await expect(page.getByText('Input Voltage')).toBeVisible()
    await expect(page.getByText('ASIC Voltage')).toBeVisible()
  })
})
