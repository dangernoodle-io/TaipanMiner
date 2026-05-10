import { describe, it, expect, beforeEach } from 'vitest'
import { render, fireEvent } from '@testing-library/svelte'
import WifiSelect from './WifiSelect.svelte'

describe('WifiSelect', () => {
  const mockNetworks = [
    { ssid: 'HomeWifi', rssi: -45, secure: true },
    { ssid: 'GuestNet', rssi: -70, secure: false },
    { ssid: 'WeakSignal', rssi: -85, secure: true },
  ]

  it('renders trigger button', () => {
    const { getByRole } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: '' },
    })
    expect(getByRole('button')).toBeInTheDocument()
  })

  it('displays empty state when no network selected and list not empty', () => {
    const { getByText } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: '' },
    })
    expect(getByText('No networks found')).toBeInTheDocument()
  })

  it('displays empty state with custom label', () => {
    const { getByText } = render(WifiSelect, {
      props: { networks: [], selected: '', emptyLabel: 'No WiFi available' },
    })
    expect(getByText('No WiFi available')).toBeInTheDocument()
  })

  it('displays scanning state when scanning prop is true', () => {
    const { getByText } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: '', scanning: true },
    })
    expect(getByText('Scanning…')).toBeInTheDocument()
  })

  it('displays custom scanning label', () => {
    const { getByText } = render(WifiSelect, {
      props: {
        networks: mockNetworks,
        selected: '',
        scanning: true,
        scanningLabel: 'Looking for networks...',
      },
    })
    expect(getByText('Looking for networks...')).toBeInTheDocument()
  })

  it('displays selected network SSID', () => {
    const { getByText } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: 'HomeWifi' },
    })
    expect(getByText('HomeWifi')).toBeInTheDocument()
  })

  it('opens dropdown when trigger is clicked', async () => {
    const { getByRole, queryByRole } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: '' },
    })
    const trigger = getByRole('button')

    await fireEvent.click(trigger)
    const listbox = queryByRole('listbox')
    expect(listbox).toBeInTheDocument()
  })

  it('closes dropdown when network is selected', async () => {
    const { getByRole, queryByRole, getAllByRole } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: '' },
    })
    const trigger = getByRole('button')

    await fireEvent.click(trigger)
    let listbox = queryByRole('listbox')
    expect(listbox).toBeInTheDocument()

    const options = getAllByRole('option')
    const firstOption = options[0]
    await fireEvent.click(firstOption)

    listbox = queryByRole('listbox')
    expect(listbox).not.toBeInTheDocument()
  })

  it('updates selected when network is picked', async () => {
    const { getByRole, getAllByRole, getByText } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: '' },
    })
    const trigger = getByRole('button')

    await fireEvent.click(trigger)
    const options = getAllByRole('option')
    const secondOption = options[1]

    await fireEvent.click(secondOption)
    // After selection, the SSID should be displayed in the trigger
    expect(getByText('GuestNet')).toBeInTheDocument()
  })

  it('disables trigger when disabled prop is true', () => {
    const { getByRole } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: '', disabled: true },
    })
    const trigger = getByRole('button')
    expect(trigger).toBeDisabled()
  })

  it('disables trigger when scanning is true', () => {
    const { getByRole } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: '', scanning: true },
    })
    const trigger = getByRole('button')
    expect(trigger).toBeDisabled()
  })

  it('displays manual entry option when allowManualEntry is true', async () => {
    const { getByRole, getByText, getAllByRole } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: '', allowManualEntry: true },
    })
    const trigger = getByRole('button')

    await fireEvent.click(trigger)
    expect(getByText('Manual entry…')).toBeInTheDocument()
  })

  it('hides manual entry option when allowManualEntry is false', async () => {
    const { getByRole, queryByText } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: '', allowManualEntry: false },
    })
    const trigger = getByRole('button')

    await fireEvent.click(trigger)
    expect(queryByText('Manual entry…')).not.toBeInTheDocument()
  })

  it('selects manual entry when clicked', async () => {
    const { getByRole, getByText } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: '', allowManualEntry: true, manualSelectedLabel: 'Manual Entry' },
    })
    const trigger = getByRole('button')

    await fireEvent.click(trigger)
    const manualBtn = getByText('Manual entry…')
    await fireEvent.click(manualBtn)

    // After selecting manual entry, the manual label should be shown
    expect(getByText('Manual Entry')).toBeInTheDocument()
  })

  it('displays manual entry label when selected', () => {
    const { getByText } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: '__manual__', manualSelectedLabel: 'Custom SSID' },
    })
    expect(getByText('Custom SSID')).toBeInTheDocument()
  })

  it('displays error message when error prop is set', () => {
    const { getByText } = render(WifiSelect, {
      props: { networks: [], selected: '', error: 'WiFi scan failed' },
    })
    expect(getByText('WiFi scan failed')).toBeInTheDocument()
  })

  it('renders security lock icon for secure networks', () => {
    const { container, getByRole } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: 'HomeWifi' },
    })
    // HomeWifi is secure, should show lock icon in the trigger
    const header = getByRole('button')
    expect(header.querySelector('svg')).toBeInTheDocument()
  })

  it('shows rssi signal strength bars', () => {
    const { container, getByRole } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: 'HomeWifi' },
    })
    const header = getByRole('button')
    const svgs = header.querySelectorAll('svg')
    // Should have signal strength SVG
    expect(svgs.length).toBeGreaterThan(0)
  })

  it('closes dropdown when clicking outside', async () => {
    const { getByRole, container, queryByRole } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: '' },
    })
    const trigger = getByRole('button')

    await fireEvent.click(trigger)
    expect(queryByRole('listbox')).toBeInTheDocument()

    // Click on document body outside the component
    await fireEvent.click(document.body)
    expect(queryByRole('listbox')).not.toBeInTheDocument()
  })

  it('trigger has aria-haspopup and aria-expanded attributes', async () => {
    const { getByRole } = render(WifiSelect, {
      props: { networks: mockNetworks, selected: '' },
    })
    const trigger = getByRole('button')

    expect(trigger).toHaveAttribute('aria-haspopup', 'listbox')
    expect(trigger).toHaveAttribute('aria-expanded', 'false')

    await fireEvent.click(trigger)
    expect(trigger).toHaveAttribute('aria-expanded', 'true')
  })
})
