import assert from 'node:assert/strict'
import * as api from '../web/src/composables/useApi.js'

assert.equal(typeof api.getBuiltinCards, 'function')
assert.equal(typeof api.checkBuiltinCardRealName, 'function')
assert.equal(typeof api.switchBuiltinCard, 'function')
assert.equal(typeof api.goformProxy, 'function')
