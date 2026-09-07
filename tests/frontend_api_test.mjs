import assert from 'node:assert/strict'
import * as api from '../web/src/composables/useApi.js'

assert.equal(typeof api.getCapabilities, 'function')
assert.equal(typeof api.getWifiStatus, 'function')
assert.equal(typeof api.setWifiConfig, 'function')
assert.equal(typeof api.getWifiClients, 'function')
assert.equal(typeof api.getWifiBlacklist, 'function')
assert.equal(typeof api.getWifiWhitelist, 'function')
assert.equal(typeof api.lookupMacVendor, 'function')
assert.equal(typeof api.getBuiltinCards, 'function')
assert.equal(typeof api.approveBuiltinCardSwitch, 'function')
assert.equal(typeof api.switchBuiltinCardGoform, 'function')
assert.equal(typeof api.getAdbStatus, 'function')
assert.equal(typeof api.setAdbWireless, 'function')
assert.equal(typeof api.setAdbUsb, 'function')
assert.equal(typeof api.restartAdb, 'function')
