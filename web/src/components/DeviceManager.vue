<script setup>
import { ref, computed, onMounted, onUnmounted, inject } from 'vue'
import { useToast } from '../composables/useToast'
import { 
  getWifiClients, getWifiBlacklist, getWifiWhitelist,
  lookupMacVendor,
  addToWifiBlacklist, addToWifiWhitelist,
  removeFromWifiBlacklist, removeFromWifiWhitelist,
  clearWifiBlacklist, clearWifiWhitelist
} from '../composables/useApi'
import { on, off } from '../composables/useEventBus'

const { success, error } = useToast()
const capabilities = inject('capabilities', ref(null))

const activeTab = ref('all')
const tabs = [
  { id: 'all', label: '全部设备', icon: 'fa-laptop', color: 'from-blue-500 to-cyan-400' },
  { id: 'blacklist', label: '黑名单', icon: 'fa-ban', color: 'from-red-500 to-rose-400' },
  { id: 'whitelist', label: '白名单', icon: 'fa-shield-alt', color: 'from-green-500 to-emerald-400' }
]

const clients = ref([])
const blacklist = ref([])
const whitelist = ref([])
const vendorByOui = ref(loadVendorCache())
const loading = ref(false)
const expandedMac = ref(null)

function loadVendorCache() {
  try {
    return JSON.parse(localStorage.getItem('udx710_oui_vendor_cache') || '{}')
  } catch {
    return {}
  }
}

function macToOui(mac) {
  const normalized = String(mac || '').toUpperCase().replace(/-/g, ':')
  if (!/^([0-9A-F]{2}:){5}[0-9A-F]{2}$/.test(normalized)) return null
  if ((parseInt(normalized.slice(0, 2), 16) & 2) !== 0) return null
  return normalized.slice(0, 8)
}

function vendorLabel(mac) {
  const oui = macToOui(mac)
  if (!oui) return '随机 MAC'
  return vendorByOui.value[oui] || '查询中…'
}

async function loadVendors(devices) {
  const ouis = [...new Set(devices.map(device => macToOui(device.mac)).filter(Boolean))]
  const pending = ouis.filter(oui => !vendorByOui.value[oui])
  await Promise.all(pending.map(async oui => {
    try {
      const result = await lookupMacVendor(oui)
      vendorByOui.value[oui] = result.company || '未知厂商'
    } catch {
      vendorByOui.value[oui] = '未知厂商'
    }
  }))
  localStorage.setItem('udx710_oui_vendor_cache', JSON.stringify(vendorByOui.value))
}

const filteredDevices = computed(() => {
  if (activeTab.value === 'all') {
    // 排除黑名单和白名单中的MAC
    return clients.value.filter(c => !blacklist.value.includes(c.mac) && !whitelist.value.includes(c.mac))
  }
  if (activeTab.value === 'blacklist') return blacklist.value.map(mac => ({ mac }))
  return whitelist.value.map(mac => ({ mac }))
})

function formatSignal(signal) {
  if (!signal && signal !== 0) return { text: '-', color: 'text-slate-400', icon: 'signal' }
  const val = parseInt(signal)
  if (val >= -50) return { text: '极好', color: 'text-green-500', icon: 'signal' }
  if (val >= -60) return { text: '良好', color: 'text-blue-500', icon: 'signal' }
  if (val >= -70) return { text: '一般', color: 'text-yellow-500', icon: 'signal' }
  return { text: '较弱', color: 'text-red-500', icon: 'signal' }
}

function formatConnectedTime(seconds) {
  if (!seconds && seconds !== 0) return '-'
  const s = parseInt(seconds)
  if (s < 60) return `${s}秒`
  if (s < 3600) return `${Math.floor(s / 60)}分${s % 60}秒`
  const h = Math.floor(s / 3600)
  const m = Math.floor((s % 3600) / 60)
  return `${h}小时${m}分`
}

function formatRate(kbps) {
  if (!kbps && kbps !== 0) return '0 Kbps'
  if (kbps < 1000) return `${kbps} Kbps`
  return `${(kbps / 1000).toFixed(1)} Mbps`
}

function accessTypeLabel(type) {
  return { wifi: 'Wi‑Fi', usb: 'USB', rj45: 'RJ45', bridge: '网桥' }[type] || '未知'
}

function deviceIps(device) {
  return [device.ipv4, device.ipv6].filter(Boolean).join(' / ') || '-'
}

async function loadData() {
  loading.value = true
  try {
    const [clientsRes, blacklistRes, whitelistRes] = await Promise.all([
      getWifiClients(),
      getWifiBlacklist().catch(() => ({ blacklist: [] })),
      getWifiWhitelist().catch(() => ({ whitelist: [] }))
    ])
    // API返回数组或{clients:[]}对象
    clients.value = Array.isArray(clientsRes) ? clientsRes : (clientsRes.clients || [])
    blacklist.value = Array.isArray(blacklistRes) ? blacklistRes : (blacklistRes.blacklist || [])
    whitelist.value = Array.isArray(whitelistRes) ? whitelistRes : (whitelistRes.whitelist || [])
    loadVendors(clients.value)
  } catch (e) {
    clients.value = []
    error(e.message || '获取接入设备失败')
  }
  finally { loading.value = false }
}

async function handleAddToBlacklist(mac) {
  try { await addToWifiBlacklist(mac); success(`已将 ${mac} 加入黑名单`); await loadData() }
  catch (e) { error('操作失败') }
}
async function handleAddToWhitelist(mac) {
  try { await addToWifiWhitelist(mac); success(`已将 ${mac} 加入白名单`); await loadData() }
  catch (e) { error('操作失败') }
}
async function handleRemoveFromBlacklist(mac) {
  try { await removeFromWifiBlacklist(mac); success(`已将 ${mac} 从黑名单移除`); await loadData() }
  catch (e) { error('操作失败') }
}
async function handleRemoveFromWhitelist(mac) {
  try { await removeFromWifiWhitelist(mac); success(`已将 ${mac} 从白名单移除`); await loadData() }
  catch (e) { error('操作失败') }
}
async function handleClearBlacklist() {
  try { await clearWifiBlacklist(); success('已清空黑名单'); await loadData() }
  catch (e) { error('操作失败') }
}
async function handleClearWhitelist() {
  try { await clearWifiWhitelist(); success('已清空白名单'); await loadData() }
  catch (e) { error('操作失败') }
}
function toggleExpand(mac) { expandedMac.value = expandedMac.value === mac ? null : mac }

onMounted(() => {
  loadData()
  on('wifi-changed', loadData)
})
onUnmounted(() => {
  off('wifi-changed', loadData)
})
</script>

<template>
  <div class="space-y-4">
    <div v-if="capabilities" class="grid grid-cols-1 md:grid-cols-3 gap-3">
      <div class="rounded-2xl bg-white/95 dark:bg-white/5 border border-slate-200/60 dark:border-white/10 p-4">
        <p class="text-slate-500 dark:text-white/50 text-xs">RJ45</p>
        <p class="font-semibold text-slate-900 dark:text-white">
          {{ capabilities.rj45?.link_up ? '已连接' : capabilities.rj45?.usable ? '已识别，未连接' : '物理存在，系统侧不可用' }}
        </p>
      </div>
      <div class="rounded-2xl bg-white/95 dark:bg-white/5 border border-slate-200/60 dark:border-white/10 p-4">
        <p class="text-slate-500 dark:text-white/50 text-xs">Type-C</p>
        <p class="font-semibold text-slate-900 dark:text-white">
          {{ capabilities.typec?.link_up ? 'RNDIS 已连接' : capabilities.typec?.rndis ? 'RNDIS 已启用，未连接' : '未启用' }}
        </p>
      </div>
      <div class="rounded-2xl bg-white/95 dark:bg-white/5 border border-slate-200/60 dark:border-white/10 p-4">
        <p class="text-slate-500 dark:text-white/50 text-xs">蜂窝数据接口</p>
        <p class="font-semibold text-slate-900 dark:text-white">
          {{ capabilities.cellular?.interface || '不可用' }}
        </p>
      </div>
    </div>

    <!-- 标签页切换 -->
    <div class="rounded-2xl bg-white/95 dark:bg-white/5 border border-slate-200/60 dark:border-white/10 p-2 shadow-lg shadow-slate-200/40 dark:shadow-black/20">
      <div class="flex space-x-1">
        <button v-for="tab in tabs" :key="tab.id" @click="activeTab = tab.id"
          class="flex-1 py-2.5 px-3 rounded-xl text-sm font-medium transition-all duration-200"
          :class="activeTab === tab.id ? `bg-gradient-to-r ${tab.color} text-white shadow-lg` : 'text-slate-600 dark:text-white/60 hover:bg-slate-100 dark:hover:bg-white/10'">
          <font-awesome-icon :icon="tab.icon.replace('fa-', '')" class="mr-1.5" />
          <span class="hidden sm:inline">{{ tab.label }}</span>
          <span class="sm:hidden">{{ tab.label.replace('名单', '') }}</span>
        </button>
      </div>
    </div>

    <!-- 设备统计 -->
    <div class="rounded-2xl bg-white/95 dark:bg-white/5 border border-slate-200/60 dark:border-white/10 p-4 shadow-lg shadow-slate-200/40 dark:shadow-black/20 hover:shadow-xl hover:shadow-slate-300/50 dark:hover:shadow-black/30 transition-all duration-300">
      <div class="flex items-center justify-between flex-wrap gap-4">
        <div class="flex items-center space-x-6">
          <div class="flex items-center space-x-2">
            <font-awesome-icon icon="wifi" class="text-blue-500" />
            <span class="text-slate-500 dark:text-white/50 text-sm">已连接</span>
            <span class="text-xl font-bold text-slate-900 dark:text-white">{{ clients.length }}</span>
          </div>
          <div class="flex items-center space-x-2">
            <font-awesome-icon icon="ban" class="text-red-500" />
            <span class="text-slate-500 dark:text-white/50 text-sm">黑名单</span>
            <span class="text-xl font-bold text-slate-900 dark:text-white">{{ blacklist.length }}</span>
          </div>
          <div class="flex items-center space-x-2">
            <font-awesome-icon icon="shield-alt" class="text-green-500" />
            <span class="text-slate-500 dark:text-white/50 text-sm">白名单</span>
            <span class="text-xl font-bold text-slate-900 dark:text-white">{{ whitelist.length }}</span>
          </div>
        </div>
        <div class="flex items-center space-x-2">
          <button v-if="activeTab === 'blacklist' && blacklist.length > 0" @click="handleClearBlacklist"
            class="px-3 py-1.5 bg-red-500/10 hover:bg-red-500/20 text-red-500 rounded-lg text-sm font-medium transition-colors">
            <font-awesome-icon icon="trash-alt" class="mr-1" />清空
          </button>
          <button v-if="activeTab === 'whitelist' && whitelist.length > 0" @click="handleClearWhitelist"
            class="px-3 py-1.5 bg-orange-500/10 hover:bg-orange-500/20 text-orange-500 rounded-lg text-sm font-medium transition-colors">
            <font-awesome-icon icon="trash-alt" class="mr-1" />清空
          </button>
        </div>
      </div>
    </div>

    <!-- 桌面端表格 -->
    <div class="hidden md:block rounded-2xl bg-white/95 dark:bg-white/5 border border-slate-200/60 dark:border-white/10 overflow-hidden shadow-lg shadow-slate-200/40 dark:shadow-black/20">
      <table class="w-full">
        <thead>
          <tr class="bg-slate-50 dark:bg-white/5 border-b border-slate-200 dark:border-white/10">
            <th class="text-left py-3 px-4 text-slate-500 dark:text-white/50 text-sm font-medium">MAC地址</th>
            <th class="text-left py-3 px-4 text-slate-500 dark:text-white/50 text-sm font-medium">接入方式</th>
            <th class="text-left py-3 px-4 text-slate-500 dark:text-white/50 text-sm font-medium">IP地址</th>
            <th v-if="activeTab === 'all'" class="text-center py-3 px-4 text-slate-500 dark:text-white/50 text-sm font-medium">上传速率</th>
            <th v-if="activeTab === 'all'" class="text-center py-3 px-4 text-slate-500 dark:text-white/50 text-sm font-medium">下载速率</th>
            <th v-if="activeTab === 'all'" class="text-center py-3 px-4 text-slate-500 dark:text-white/50 text-sm font-medium">总速率</th>
            <th v-if="activeTab === 'all'" class="text-center py-3 px-4 text-slate-500 dark:text-white/50 text-sm font-medium">信号</th>
            <th v-if="activeTab === 'all'" class="text-center py-3 px-4 text-slate-500 dark:text-white/50 text-sm font-medium">连接时长</th>
            <th class="text-right py-3 px-4 text-slate-500 dark:text-white/50 text-sm font-medium pr-6">操作</th>
          </tr>
        </thead>
        <tbody>
          <tr v-for="device in filteredDevices" :key="device.mac" class="border-b border-slate-100 dark:border-white/5 hover:bg-slate-50 dark:hover:bg-white/5 transition-colors">
            <td class="py-3 px-4">
              <div class="flex items-center space-x-3">
                <div class="w-8 h-8 rounded-lg bg-gradient-to-br from-blue-500/20 to-cyan-500/20 flex items-center justify-center flex-shrink-0">
                  <font-awesome-icon icon="laptop" class="text-blue-500 text-sm" />
                </div>
                <span class="font-mono text-slate-900 dark:text-white text-sm">{{ device.mac }}</span>
                <span class="block text-xs text-slate-400">{{ vendorLabel(device.mac) }}</span>
              </div>
            </td>
            <td class="py-3 px-4">
              <span class="px-2 py-1 rounded-lg bg-blue-500/10 text-blue-500 text-xs">{{ accessTypeLabel(device.access_type) }}</span>
              <span v-if="device.interface" class="ml-2 text-xs text-slate-400">{{ device.interface }}</span>
            </td>
            <td class="py-3 px-4 text-sm text-slate-600 dark:text-white/70 font-mono">{{ deviceIps(device) }}</td>
            <td v-if="activeTab === 'all'" class="py-3 px-4 text-center">
              <span class="text-green-500 text-sm font-medium"><font-awesome-icon icon="arrow-up" class="mr-1 text-xs" />{{ formatRate(device.tx_bytes) }}</span>
            </td>
            <td v-if="activeTab === 'all'" class="py-3 px-4 text-center">
              <span class="text-blue-500 text-sm font-medium"><font-awesome-icon icon="arrow-down" class="mr-1 text-xs" />{{ formatRate(device.rx_bytes) }}</span>
            </td>
            <td v-if="activeTab === 'all'" class="py-3 px-4 text-center">
              <span class="text-slate-900 dark:text-white text-sm font-semibold">{{ formatRate(device.tx_bytes + device.rx_bytes) }}</span>
            </td>
            <td v-if="activeTab === 'all'" class="py-3 px-4 text-center">
              <span :class="formatSignal(device.signal).color" class="text-sm font-medium">
                <font-awesome-icon :icon="formatSignal(device.signal).icon" class="mr-1 text-xs" />{{ device.signal }} dBm
              </span>
            </td>
            <td v-if="activeTab === 'all'" class="py-3 px-4 text-center">
              <span class="text-slate-900 dark:text-white text-sm font-medium">
                <font-awesome-icon icon="clock" class="mr-1 text-xs text-purple-500" />{{ formatConnectedTime(device.connected_time) }}
              </span>
            </td>
            <td class="py-3 px-4">
              <div class="flex items-center justify-end space-x-2">
                <template v-if="activeTab === 'all'">
                  <button @click="handleAddToBlacklist(device.mac)" class="px-3 py-1.5 bg-red-500/10 hover:bg-red-500/20 text-red-500 rounded-lg text-xs font-medium transition-colors">
                    <font-awesome-icon icon="ban" class="mr-1" />拉黑
                  </button>
                  <button @click="handleAddToWhitelist(device.mac)" class="px-3 py-1.5 bg-green-500/10 hover:bg-green-500/20 text-green-500 rounded-lg text-xs font-medium transition-colors">
                    <font-awesome-icon icon="shield-alt" class="mr-1" />加白
                  </button>
                </template>
                <template v-else-if="activeTab === 'blacklist'">
                  <button @click="handleRemoveFromBlacklist(device.mac)" class="px-3 py-1.5 bg-blue-500/10 hover:bg-blue-500/20 text-blue-500 rounded-lg text-xs font-medium transition-colors">
                    <font-awesome-icon icon="undo" class="mr-1" />移除
                  </button>
                </template>
                <template v-else>
                  <button @click="handleRemoveFromWhitelist(device.mac)" class="px-3 py-1.5 bg-orange-500/10 hover:bg-orange-500/20 text-orange-500 rounded-lg text-xs font-medium transition-colors">
                    <font-awesome-icon icon="undo" class="mr-1" />移除
                  </button>
                </template>
              </div>
            </td>
          </tr>
        </tbody>
      </table>
      <div v-if="filteredDevices.length === 0" class="py-12 text-center">
        <font-awesome-icon icon="inbox" class="text-4xl text-slate-300 dark:text-white/20 mb-3" />
        <p class="text-slate-500 dark:text-white/50">暂无设备</p>
      </div>
    </div>

    <!-- 移动端卡片 -->
    <div class="md:hidden space-y-3">
      <div v-for="device in filteredDevices" :key="device.mac" @click="toggleExpand(device.mac)"
        class="rounded-2xl bg-white/95 dark:bg-white/5 border border-slate-200/60 dark:border-white/10 p-4 shadow-md shadow-slate-200/30 dark:shadow-black/10 hover:shadow-lg hover:shadow-slate-300/40 dark:hover:shadow-black/20 transition-all duration-300"
        :class="expandedMac === device.mac ? 'ring-2 ring-indigo-500/50' : ''">
        <div class="flex items-center justify-between mb-3">
          <div class="flex items-center space-x-3">
            <div class="w-10 h-10 rounded-xl bg-gradient-to-br from-blue-500/20 to-cyan-500/20 flex items-center justify-center">
              <font-awesome-icon icon="laptop" class="text-blue-500" />
            </div>
          <p class="font-mono text-slate-900 dark:text-white text-sm font-medium">{{ device.mac }}</p>
          <p class="text-xs text-slate-500 dark:text-white/50">{{ vendorLabel(device.mac) }}</p>
          <p class="text-xs text-blue-500">{{ accessTypeLabel(device.access_type) }} · {{ deviceIps(device) }}</p>
          </div>
          <font-awesome-icon :icon="expandedMac === device.mac ? 'chevron-up' : 'chevron-down'" class="text-slate-400 dark:text-white/40 text-sm" />
        </div>
        
        <div v-if="activeTab === 'all'" class="grid grid-cols-3 gap-2 mb-2">
          <div class="bg-slate-50 dark:bg-white/5 rounded-xl p-2 text-center">
            <p class="text-green-500 text-xs mb-0.5"><font-awesome-icon icon="arrow-up" class="mr-1" />上传速率</p>
            <p class="text-slate-900 dark:text-white text-sm font-semibold">{{ formatRate(device.tx_bytes) }}</p>
          </div>
          <div class="bg-slate-50 dark:bg-white/5 rounded-xl p-2 text-center">
            <p class="text-blue-500 text-xs mb-0.5"><font-awesome-icon icon="arrow-down" class="mr-1" />下载速率</p>
            <p class="text-slate-900 dark:text-white text-sm font-semibold">{{ formatRate(device.rx_bytes) }}</p>
          </div>
          <div class="bg-slate-50 dark:bg-white/5 rounded-xl p-2 text-center">
            <p class="text-purple-500 text-xs mb-0.5"><font-awesome-icon icon="chart-pie" class="mr-1" />总速率</p>
            <p class="text-slate-900 dark:text-white text-sm font-semibold">{{ formatRate(device.tx_bytes + device.rx_bytes) }}</p>
          </div>
        </div>
        
        <div v-if="activeTab === 'all'" class="grid grid-cols-2 gap-2 mb-3">
          <div class="bg-slate-50 dark:bg-white/5 rounded-xl p-2 text-center">
            <p :class="formatSignal(device.signal).color" class="text-xs mb-0.5">
              <font-awesome-icon :icon="formatSignal(device.signal).icon" class="mr-1" />信号
            </p>
            <p class="text-slate-900 dark:text-white text-sm font-semibold">{{ device.signal }} dBm</p>
          </div>
          <div class="bg-slate-50 dark:bg-white/5 rounded-xl p-2 text-center">
            <p class="text-purple-500 text-xs mb-0.5"><font-awesome-icon icon="clock" class="mr-1" />连接时长</p>
            <p class="text-slate-900 dark:text-white text-sm font-semibold">{{ formatConnectedTime(device.connected_time) }}</p>
          </div>
        </div>
        
        <Transition name="slide">
          <div v-if="expandedMac === device.mac" class="pt-3 border-t border-slate-200 dark:border-white/10">
            <div class="flex space-x-2 justify-end">
              <template v-if="activeTab === 'all'">
                <button @click.stop="handleAddToBlacklist(device.mac)" class="px-4 py-2 bg-red-500/10 hover:bg-red-500/20 text-red-500 rounded-xl text-sm font-medium transition-colors">
                  <font-awesome-icon icon="ban" class="mr-1.5" />拉黑
                </button>
                <button @click.stop="handleAddToWhitelist(device.mac)" class="px-4 py-2 bg-green-500/10 hover:bg-green-500/20 text-green-500 rounded-xl text-sm font-medium transition-colors">
                  <font-awesome-icon icon="shield-alt" class="mr-1.5" />加白名单
                </button>
              </template>
              <template v-else-if="activeTab === 'blacklist'">
                <button @click.stop="handleRemoveFromBlacklist(device.mac)" class="px-4 py-2 bg-blue-500/10 hover:bg-blue-500/20 text-blue-500 rounded-xl text-sm font-medium transition-colors">
                  <font-awesome-icon icon="undo" class="mr-1.5" />移除黑名单
                </button>
              </template>
              <template v-else>
                <button @click.stop="handleRemoveFromWhitelist(device.mac)" class="px-4 py-2 bg-orange-500/10 hover:bg-orange-500/20 text-orange-500 rounded-xl text-sm font-medium transition-colors">
                  <font-awesome-icon icon="undo" class="mr-1.5" />移除白名单
                </button>
              </template>
            </div>
          </div>
        </Transition>
      </div>
      
      <div v-if="filteredDevices.length === 0" class="rounded-2xl bg-white/95 dark:bg-white/5 border border-slate-200/60 dark:border-white/10 py-12 text-center shadow-lg shadow-slate-200/40 dark:shadow-black/20">
        <font-awesome-icon icon="inbox" class="text-4xl text-slate-300 dark:text-white/20 mb-3" />
        <p class="text-slate-500 dark:text-white/50">暂无设备</p>
      </div>
    </div>
  </div>
</template>

<style scoped>
.slide-enter-active { transition: all 0.2s ease-out; }
.slide-leave-active { transition: all 0.15s ease-in; }
.slide-enter-from, .slide-leave-to { opacity: 0; max-height: 0; padding-top: 0; padding-bottom: 0; margin-top: 0; overflow: hidden; }
.slide-enter-to, .slide-leave-from { opacity: 1; max-height: 100px; }
</style>
