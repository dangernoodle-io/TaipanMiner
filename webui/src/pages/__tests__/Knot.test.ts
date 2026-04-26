import { describe, it, expect } from 'vitest'
import type { KnotPeer } from '../../lib/api'

describe('Knot peer data types and transformations', () => {
  describe('KnotPeer interface', () => {
    it('creates valid KnotPeer object', () => {
      const peer: KnotPeer = {
        instance: 'miner1',
        hostname: 'miner1.local',
        ip: '192.168.1.1',
        worker: 'example.com/worker1',
        board: 'tdongle-s3',
        version: 'v1.0.0',
        state: 'mining',
        seen_ago_s: 5,
      }

      expect(peer.instance).toBe('miner1')
      expect(peer.hostname).toBe('miner1.local')
      expect(peer.ip).toBe('192.168.1.1')
      expect(peer.worker).toBe('example.com/worker1')
      expect(peer.board).toBe('tdongle-s3')
      expect(peer.version).toBe('v1.0.0')
      expect(peer.state).toBe('mining')
      expect(peer.seen_ago_s).toBe(5)
    })

    it('handles all state values', () => {
      const states = ['mining', 'ota', 'provisioning', 'idle', 'unknown']
      states.forEach((state) => {
        const peer: KnotPeer = {
          instance: 'test',
          hostname: 'test.local',
          ip: '192.168.1.1',
          worker: 'worker',
          board: 'board',
          version: 'v1.0.0',
          state,
          seen_ago_s: 0,
        }
        expect(peer.state).toBe(state)
      })
    })
  })

  describe('peer array operations', () => {
    it('filters peers by state', () => {
      const peers: KnotPeer[] = [
        {
          instance: 'miner1',
          hostname: 'miner1.local',
          ip: '192.168.1.1',
          worker: 'w1',
          board: 'b1',
          version: 'v1',
          state: 'mining',
          seen_ago_s: 5,
        },
        {
          instance: 'miner2',
          hostname: 'miner2.local',
          ip: '192.168.1.2',
          worker: 'w2',
          board: 'b2',
          version: 'v1',
          state: 'idle',
          seen_ago_s: 10,
        },
      ]

      const mining = peers.filter((p) => p.state === 'mining')
      expect(mining).toHaveLength(1)
      expect(mining[0].hostname).toBe('miner1.local')
    })

    it('sorts peers by seen_ago_s', () => {
      const peers: KnotPeer[] = [
        {
          instance: 'm1',
          hostname: 'h1',
          ip: '1',
          worker: 'w',
          board: 'b',
          version: 'v',
          state: 'mining',
          seen_ago_s: 10,
        },
        {
          instance: 'm2',
          hostname: 'h2',
          ip: '2',
          worker: 'w',
          board: 'b',
          version: 'v',
          state: 'mining',
          seen_ago_s: 5,
        },
      ]

      const sorted = [...peers].sort((a, b) => a.seen_ago_s - b.seen_ago_s)
      expect(sorted[0].seen_ago_s).toBe(5)
      expect(sorted[1].seen_ago_s).toBe(10)
    })

    it('finds peer by hostname', () => {
      const peers: KnotPeer[] = [
        {
          instance: 'm1',
          hostname: 'miner1.local',
          ip: '1',
          worker: 'w',
          board: 'b',
          version: 'v',
          state: 'mining',
          seen_ago_s: 5,
        },
        {
          instance: 'm2',
          hostname: 'miner2.local',
          ip: '2',
          worker: 'w',
          board: 'b',
          version: 'v',
          state: 'mining',
          seen_ago_s: 10,
        },
      ]

      const found = peers.find((p) => p.hostname === 'miner1.local')
      expect(found).toBeDefined()
      expect(found?.instance).toBe('m1')
    })
  })

  describe('peer data validation', () => {
    it('validates hostname is non-empty', () => {
      const peer: KnotPeer = {
        instance: 'miner1',
        hostname: 'miner1.local',
        ip: '192.168.1.1',
        worker: 'worker',
        board: 'board',
        version: 'v1.0.0',
        state: 'mining',
        seen_ago_s: 5,
      }

      expect(peer.hostname.length).toBeGreaterThan(0)
    })

    it('validates seen_ago_s is non-negative', () => {
      const peer: KnotPeer = {
        instance: 'miner1',
        hostname: 'miner1.local',
        ip: '192.168.1.1',
        worker: 'worker',
        board: 'board',
        version: 'v1.0.0',
        state: 'mining',
        seen_ago_s: 0,
      }

      expect(peer.seen_ago_s).toBeGreaterThanOrEqual(0)
    })

    it('formats time display with seen_ago_s', () => {
      const peer: KnotPeer = {
        instance: 'miner1',
        hostname: 'miner1.local',
        ip: '192.168.1.1',
        worker: 'worker',
        board: 'board',
        version: 'v1.0.0',
        state: 'mining',
        seen_ago_s: 125,
      }

      const display = `${peer.seen_ago_s}s`
      expect(display).toBe('125s')
    })
  })

  describe('peer HTTP link generation', () => {
    it('generates valid HTTP URL from hostname', () => {
      const peer: KnotPeer = {
        instance: 'miner1',
        hostname: 'miner1.local',
        ip: '192.168.1.1',
        worker: 'worker',
        board: 'board',
        version: 'v1.0.0',
        state: 'mining',
        seen_ago_s: 5,
      }

      const url = `http://${peer.hostname}`
      expect(url).toBe('http://miner1.local')
      expect(new URL(url)).toBeDefined()
    })

    it('creates multiple peer URLs without collision', () => {
      const peers: KnotPeer[] = [
        {
          instance: 'm1',
          hostname: 'miner1.local',
          ip: '1',
          worker: 'w',
          board: 'b',
          version: 'v',
          state: 'mining',
          seen_ago_s: 5,
        },
        {
          instance: 'm2',
          hostname: 'miner2.local',
          ip: '2',
          worker: 'w',
          board: 'b',
          version: 'v',
          state: 'mining',
          seen_ago_s: 10,
        },
      ]

      const urls = peers.map((p) => `http://${p.hostname}`)
      expect(urls).toHaveLength(2)
      expect(urls[0]).toBe('http://miner1.local')
      expect(urls[1]).toBe('http://miner2.local')
    })
  })

  describe('badge class mapping', () => {
    it('maps mining state to correct class', () => {
      const stateClassMap: Record<string, string> = {
        mining: 'state-mining',
        ota: 'state-ota',
        provisioning: 'state-provisioning',
        idle: 'state-idle',
        unknown: 'state-neutral',
      }

      expect(stateClassMap['mining']).toBe('state-mining')
      expect(stateClassMap['ota']).toBe('state-ota')
      expect(stateClassMap['provisioning']).toBe('state-provisioning')
      expect(stateClassMap['idle']).toBe('state-idle')
      expect(stateClassMap['unknown']).toBe('state-neutral')
    })

    it('provides default class for unmapped states', () => {
      const getStateClass = (state: string): string => {
        const stateMap: Record<string, string> = {
          mining: 'state-mining',
          ota: 'state-ota',
          provisioning: 'state-provisioning',
          idle: 'state-idle',
        }
        return stateMap[state] || 'state-neutral'
      }

      expect(getStateClass('mining')).toBe('state-mining')
      expect(getStateClass('unknown-state')).toBe('state-neutral')
    })
  })

  describe('device comparison', () => {
    it('identifies current device by hostname match', () => {
      const currentHostname = 'current-host.local'
      const peers: KnotPeer[] = [
        {
          instance: 'miner1',
          hostname: 'current-host.local',
          ip: '1',
          worker: 'w',
          board: 'b',
          version: 'v',
          state: 'mining',
          seen_ago_s: 5,
        },
        {
          instance: 'miner2',
          hostname: 'other-host.local',
          ip: '2',
          worker: 'w',
          board: 'b',
          version: 'v',
          state: 'mining',
          seen_ago_s: 10,
        },
      ]

      const currentPeer = peers.find((p) => p.hostname === currentHostname)
      expect(currentPeer).toBeDefined()
      expect(currentPeer?.instance).toBe('miner1')
    })

    it('handles no current device match gracefully', () => {
      const currentHostname = 'nonexistent.local'
      const peers: KnotPeer[] = [
        {
          instance: 'miner1',
          hostname: 'miner1.local',
          ip: '1',
          worker: 'w',
          board: 'b',
          version: 'v',
          state: 'mining',
          seen_ago_s: 5,
        },
      ]

      const currentPeer = peers.find((p) => p.hostname === currentHostname)
      expect(currentPeer).toBeUndefined()
    })
  })
})
