import { test as base, expect } from '@playwright/test'
import { addCoverageReport } from 'monocart-reporter'

const test = base.extend({
  autoTestFixture: [
    async ({ page }, use, testInfo) => {
      // coverage API is chromium only
      if (testInfo.project.name === 'chromium') {
        await Promise.all([
          page.coverage.startJSCoverage({
            resetOnNavigation: false,
            reportAnonymousScripts: true,
          }),
          page.coverage.startCSSCoverage({
            resetOnNavigation: false,
          }),
        ])
      }

      await use('autoTestFixture')

      // stop coverage and add to report
      if (testInfo.project.name === 'chromium') {
        const [jsCoverage, cssCoverage] = await Promise.all([
          page.coverage.stopJSCoverage(),
          page.coverage.stopCSSCoverage(),
        ])
        const coverageList = [...jsCoverage, ...cssCoverage]
        await addCoverageReport(coverageList, testInfo)
      }
    },
    {
      scope: 'test',
      auto: true,
    },
  ],
})

export { test, expect }
