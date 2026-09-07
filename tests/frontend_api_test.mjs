import assert from 'node:assert/strict'
import * as api from '../web/src/composables/useApi.js'

assert.equal(typeof api.getCapabilities, 'function')
assert.equal(typeof api.getWifiStatus, 'function')
assert.equal(typeof api.setWifiConfig, 'function')
assert.equal(typeof api.getWifiClients, 'function')
assert.equal(typeof api.getWifiBlacklist, 'function')
assert.equal(typeof api.getWifiWhitelist, 'function')
