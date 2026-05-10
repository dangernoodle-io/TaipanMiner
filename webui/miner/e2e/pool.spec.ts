import { test, expect } from './fixtures'
import { mockMinerApi, poolFixture } from './fixtures/api'

test.describe('Pool page', () => {
  test('renders Active section with pool host:port', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/pool')

    await expect(page.getByRole('heading', { name: 'Active' })).toBeVisible()
    // host:port shown in active card status row
    await expect(page.getByText('pool.example.com:3333').first()).toBeVisible()
  })

  test('shows worker name from pool fixture', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/pool')
    // Multiple matches: active card "worker miner-1" and pool row "miner-1" cell.
    await expect(page.getByText(/worker miner-1/).first()).toBeVisible()
  })

  test('shows Primary pool row with ACTIVE pill', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/pool')

    await expect(page.getByText('Primary').first()).toBeVisible()
    await expect(page.getByText('ACTIVE').first()).toBeVisible()
  })

  test('shows + Add button when fallback is unconfigured', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/pool')

    await expect(page.getByText('Fallback').first()).toBeVisible()
    await expect(page.getByRole('button', { name: '+ Add' })).toBeVisible()
  })

  test('clicking Edit on primary opens the edit dialog with current values', async ({ page }) => {
    await mockMinerApi(page)
    await page.goto('/#/pool')

    // The primary Edit button (first Edit button in pool rows)
    await page.getByRole('button', { name: 'Edit' }).first().click()

    await expect(page.getByRole('dialog')).toBeVisible()
    await expect(page.getByRole('heading', { name: 'Primary pool' })).toBeVisible()

    // Host input is pre-populated
    const hostInput = page.locator('#pool-host')
    await expect(hostInput).toHaveValue('pool.example.com')
  })

  test('shows disconnected dot when pool.connected=false', async ({ page }) => {
    await mockMinerApi(page, {
      overrides: {
        '/api/pool': { ...poolFixture, connected: false },
      },
    })
    await page.goto('/#/pool')

    await expect(page.locator('.conn-dot.disconnected').first()).toBeVisible()
  })
})
