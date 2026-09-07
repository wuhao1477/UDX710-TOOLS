<script setup>
import { ref, onMounted, onUnmounted } from 'vue'
import { getWifiStatus, setWifiConfig, enableWifi, disableWifi, setWifiBand } from '../composables/useApi'
import { useToast } from '../composables/useToast'
import { useConfirm } from '../composables/useConfirm'
import { emit as emitEvent } from '../composables/useEventBus'

const { success, error } = useToast()
const { confirm } = useConfirm()

// WiFi状态
const wifiStatus = ref({
  enabled: false,
  band: '5G',
  ssid: '',
  password: '',
  max_clients: 32
})

// 表单数据
const form = ref({
  ssid: '',
  password: '',
  max_clients: 32
})

const showPassword = ref(false)
const showStatusPassword = ref(false)
const loading = ref(false)
const saving = ref(false)
const switching = ref(false)

// 频段选项
const bandOptions = [
  { value: '2.4G', label: '2.4GHz', icon: 'fa-signal', desc: '覆盖广，穿墙强' },
  { value: '5G', label: '5GHz', icon: 'fa-bolt', desc: '速度快，干扰少' }
]

// 获取WiFi状态
async function fetchStatus() {
  loading.value = true
  try {
    const data = await getWifiStatus()
    wifiStatus.value = {
      enabled: data.enabled || false,
      band: data.band || '5G',
      ssid: data.ssid || '',
      password: data.password || '',
      max_clients: data.max_clients || 32
    }
    // 同步表单
    form.value.ssid = data.ssid || ''
    form.value.max_clients = data.max_clients || 32
    form.value.password = ''
  } catch (err) {
    console.error('获取WiFi状态失败:', err)
    error('获取WiFi状态失败')
  } finally {
    loading.value = false
  }
}

// 切换WiFi开关
async function toggleWifi() {
  if (!await confirm({
    title: wifiStatus.value.enabled ? '关闭 WiFi' : '开启 WiFi',
    message: '设备网络状态会改变，确认继续吗？'
  })) return
  saving.value = true
  try {
    if (wifiStatus.value.enabled) {
      const res = await disableWifi()
      if (res.error) throw new Error(res.error)
      wifiStatus.value.enabled = false
      success('WiFi已关闭')
    } else {
      const res = await enableWifi(wifiStatus.value.band)
      if (res.error) throw new Error(res.error)
      wifiStatus.value.enabled = true
      success('WiFi已开启')
    }
    // 延迟刷新状态
    setTimeout(() => {
      fetchStatus()
      emitEvent('wifi-changed')
    }, 1000)
  } catch (err) {
    error('操作失败: ' + (err.message || '未知错误'))
  } finally {
    saving.value = false
  }
}

// 切换频段
async function changeBand(newBand) {
  if (newBand === wifiStatus.value.band || switching.value) return
  if (!await confirm({
    title: '切换 WiFi 频段',
    message: `将切换到 ${newBand}，当前连接可能会短暂中断。`
  })) return
  
  switching.value = true
  try {
    const res = await setWifiBand(newBand)
    if (res.error) throw new Error(res.error)
    wifiStatus.value.band = newBand
    success(`已切换到 ${newBand} 频段`)
    // 延迟刷新状态，但不显示全屏loading
    setTimeout(async () => {
      try {
        const data = await getWifiStatus()
        wifiStatus.value = {
          enabled: data.enabled || false,
          band: data.band || '5G',
          ssid: data.ssid || '',
          password: data.password || '',
          max_clients: data.max_clients || 32
        }
        form.value.ssid = data.ssid || ''
        form.value.max_clients = data.max_clients || 32
        emitEvent('wifi-changed')
      } catch (e) {
        console.error('刷新状态失败:', e)
      }
    }, 1500)
  } catch (err) {
    error('切换频段失败: ' + (err.message || '未知错误'))
  } finally {
    switching.value = false
  }
}

// 保存配置
async function saveConfig() {
  // 验证
  if (!form.value.ssid || form.value.ssid.trim() === '') {
    error('请输入WiFi名称')
    return
  }
  if (form.value.password && form.value.password.length < 8) {
    error('密码至少需要8位')
    return
  }
  if (!await confirm({
    title: '保存 WiFi 设置',
    message: form.value.password
      ? '将通过设备原生接口更新 WiFi 名称和密码，设备可能短暂断网。'
      : '将通过设备原生接口更新 WiFi 名称，设备可能短暂断网。'
  })) return
  
  saving.value = true
  try {
    const config = {
      ssid: form.value.ssid.trim()
    }
    if (form.value.password && form.value.password.length >= 8) {
      config.password = form.value.password
    }
    
    const res = await setWifiConfig(config)
    if (res.error) throw new Error(res.error)
    
    success('WiFi设置已保存')
    form.value.password = ''
    setTimeout(() => {
      fetchStatus()
      emitEvent('wifi-changed')
    }, 1000)
  } catch (err) {
    error('保存失败: ' + (err.message || '未知错误'))
  } finally {
    saving.value = false
  }
}

// 生成随机密码
function generatePassword() {
  const chars = 'ABCDEFGHJKMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789'
  let pwd = ''
  for (let i = 0; i < 12; i++) {
    pwd += chars.charAt(Math.floor(Math.random() * chars.length))
  }
  form.value.password = pwd
  showPassword.value = true
}

// 复制到剪贴板（兼容HTTP环境）
function copyText(text) {
  if (!text) return
  
  // 优先使用 Clipboard API
  if (navigator.clipboard && window.isSecureContext) {
    navigator.clipboard.writeText(text).then(() => {
      success('已复制')
    }).catch(() => {
      fallbackCopy(text)
    })
  } else {
    fallbackCopy(text)
  }
}

// 降级复制方案
function fallbackCopy(text) {
  const textarea = document.createElement('textarea')
  textarea.value = text
  textarea.style.position = 'fixed'
  textarea.style.left = '-9999px'
  document.body.appendChild(textarea)
  textarea.select()
  try {
    document.execCommand('copy')
    success('已复制')
  } catch (e) {
    error('复制失败')
  }
  document.body.removeChild(textarea)
}

onMounted(() => {
  fetchStatus()
})
</script>

<template>
  <div class="space-y-4">
    <!-- WiFi开关卡片 -->
    <div class="rounded-2xl bg-gradient-to-br from-indigo-100/80 to-purple-100/60 dark:from-indigo-500/20 dark:to-purple-500/20 border border-slate-200/60 dark:border-white/10 p-4 shadow-lg shadow-indigo-100/50 dark:shadow-black/20 hover:shadow-xl hover:shadow-indigo-200/60 dark:hover:shadow-black/30 transition-all duration-300">
      <div class="flex items-center justify-between">
        <div class="flex items-center space-x-3">
          <div class="w-12 h-12 rounded-xl bg-gradient-to-br from-indigo-500 to-purple-500 flex items-center justify-center shadow-lg">
            <i class="fas fa-wifi text-white text-xl"></i>
          </div>
          <div>
            <p class="text-slate-900 dark:text-white font-semibold">WiFi热点</p>
            <p class="text-slate-500 dark:text-white/50 text-sm">
              {{ wifiStatus.enabled ? wifiStatus.band + ' · ' + wifiStatus.ssid : '已关闭' }}
            </p>
          </div>
        </div>
        <label class="relative cursor-pointer">
          <input type="checkbox" :checked="wifiStatus.enabled" @change="toggleWifi" :disabled="saving" class="sr-only peer">
          <div class="w-14 h-7 bg-slate-300 dark:bg-white/20 rounded-full peer peer-checked:bg-green-500 transition-colors"></div>
          <div class="absolute top-0.5 left-0.5 w-6 h-6 bg-white rounded-full shadow transition-transform peer-checked:translate-x-7"></div>
        </label>
      </div>
    </div>

    <!-- 当前状态 -->
    <div class="rounded-2xl bg-white/95 dark:bg-white/5 border border-slate-200/60 dark:border-white/10 p-4 shadow-lg shadow-slate-200/40 dark:shadow-black/20 hover:shadow-xl hover:shadow-slate-300/50 dark:hover:shadow-black/30 transition-all duration-300">
      <h3 class="text-slate-900 dark:text-white font-semibold mb-3 flex items-center text-sm">
        <i class="fas fa-circle-info text-blue-400 mr-2"></i>
        当前状态
      </h3>
      <div class="space-y-2">
        <div class="flex items-center justify-between p-3 bg-slate-50 dark:bg-white/5 rounded-xl">
          <span class="text-slate-500 dark:text-white/50 text-sm">网络名称</span>
          <div class="flex items-center space-x-2">
            <span class="text-slate-900 dark:text-white font-medium text-sm">{{ wifiStatus.ssid }}</span>
            <button @click="copyText(wifiStatus.ssid)" class="text-slate-400 hover:text-indigo-400 p-1">
              <i class="fas fa-copy text-xs"></i>
            </button>
          </div>
        </div>
        <div class="flex items-center justify-between p-3 bg-slate-50 dark:bg-white/5 rounded-xl">
          <span class="text-slate-500 dark:text-white/50 text-sm">连接密码</span>
          <div class="flex items-center space-x-2">
            <span class="text-slate-900 dark:text-white font-medium font-mono text-sm">
              {{ showStatusPassword ? wifiStatus.password : '••••••••' }}
            </span>
            <button @click="showStatusPassword = !showStatusPassword" class="text-slate-400 hover:text-indigo-400 p-1">
              <i :class="showStatusPassword ? 'fas fa-eye-slash' : 'fas fa-eye'" class="text-xs"></i>
            </button>
            <button @click="copyText(wifiStatus.password)" class="text-slate-400 hover:text-indigo-400 p-1">
              <i class="fas fa-copy text-xs"></i>
            </button>
          </div>
        </div>
        <div class="flex items-center justify-between p-3 bg-slate-50 dark:bg-white/5 rounded-xl">
          <span class="text-slate-500 dark:text-white/50 text-sm">最大连接</span>
          <span class="text-slate-900 dark:text-white font-medium text-sm">{{ wifiStatus.max_clients }} 台</span>
        </div>
      </div>
    </div>

    <!-- 频段选择 -->
    <div class="rounded-2xl bg-white/95 dark:bg-white/5 border border-slate-200/60 dark:border-white/10 p-4 shadow-lg shadow-slate-200/40 dark:shadow-black/20 hover:shadow-xl hover:shadow-slate-300/50 dark:hover:shadow-black/30 transition-all duration-300">
      <h3 class="text-slate-900 dark:text-white font-semibold mb-3 flex items-center text-sm">
        <i class="fas fa-broadcast-tower text-purple-400 mr-2"></i>
        WiFi频段
      </h3>
      <div class="grid grid-cols-2 gap-3">
        <button v-for="opt in bandOptions" :key="opt.value"
          @click="changeBand(opt.value)" :disabled="switching || saving"
          class="p-3 rounded-xl text-left transition-all disabled:opacity-50 border-2"
          :class="wifiStatus.band === opt.value 
            ? 'bg-gradient-to-r from-indigo-500/10 to-purple-500/10 border-indigo-500' 
            : 'bg-slate-50 dark:bg-white/5 border-transparent hover:border-slate-300 dark:hover:border-white/20'">
          <div class="flex items-center space-x-2 mb-1">
            <i :class="'fas ' + opt.icon" class="text-indigo-400 text-sm"></i>
            <span class="font-medium text-slate-900 dark:text-white text-sm">{{ opt.label }}</span>
            <i v-if="switching && wifiStatus.band !== opt.value" class="fas fa-spinner animate-spin text-indigo-400 text-xs"></i>
          </div>
          <p class="text-xs text-slate-500 dark:text-white/50">{{ opt.desc }}</p>
        </button>
      </div>
    </div>

    <!-- WiFi设置 -->
    <div class="rounded-2xl bg-white/95 dark:bg-white/5 border border-slate-200/60 dark:border-white/10 p-4 shadow-lg shadow-slate-200/40 dark:shadow-black/20 hover:shadow-xl hover:shadow-slate-300/50 dark:hover:shadow-black/30 transition-all duration-300">
      <h3 class="text-slate-900 dark:text-white font-semibold mb-3 flex items-center text-sm">
        <i class="fas fa-cog text-cyan-400 mr-2"></i>
        WiFi设置
      </h3>
      <div class="space-y-4">
        <!-- SSID -->
        <div>
          <label class="block text-slate-500 dark:text-white/50 text-sm mb-1">WiFi名称</label>
          <input type="text" v-model="form.ssid" :disabled="saving" maxlength="32" placeholder="输入WiFi名称"
            class="w-full px-4 py-3 bg-slate-50 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white placeholder-slate-400 dark:placeholder-white/30 focus:border-indigo-400 focus:ring-2 focus:ring-indigo-400/20 transition-all disabled:opacity-50 text-sm">
        </div>
        
        <!-- 密码 -->
        <div>
          <label class="block text-slate-500 dark:text-white/50 text-sm mb-1">WiFi密码 <span class="text-xs">(留空保持不变)</span></label>
          <div class="relative">
            <input :type="showPassword ? 'text' : 'password'" v-model="form.password" :disabled="saving" 
              maxlength="63" placeholder="至少8位密码"
              class="w-full px-4 py-3 pr-20 bg-slate-50 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white placeholder-slate-400 dark:placeholder-white/30 focus:border-indigo-400 focus:ring-2 focus:ring-indigo-400/20 transition-all disabled:opacity-50 font-mono text-sm">
            <div class="absolute right-2 top-1/2 -translate-y-1/2 flex items-center">
              <button @click="showPassword = !showPassword" type="button" class="p-2 text-slate-400 hover:text-indigo-400">
                <i :class="showPassword ? 'fas fa-eye-slash' : 'fas fa-eye'" class="text-sm"></i>
              </button>
              <button @click="generatePassword" :disabled="saving" type="button" class="p-2 text-indigo-400 hover:text-indigo-300" title="生成随机密码">
                <i class="fas fa-dice text-sm"></i>
              </button>
            </div>
          </div>
          <p v-if="form.password && form.password.length < 8" class="text-red-500 text-xs mt-1">
            <i class="fas fa-exclamation-circle mr-1"></i>密码至少需要8位
          </p>
        </div>
        
        <p class="text-xs text-slate-400">最大连接数由设备网络服务管理，本页面不写入本地 hostapd 配置。</p>
        
        <!-- 保存按钮 -->
        <button @click="saveConfig" :disabled="saving || (form.password && form.password.length > 0 && form.password.length < 8)"
          class="w-full py-3 bg-gradient-to-r from-indigo-500 to-purple-500 text-white font-medium rounded-xl hover:shadow-lg hover:shadow-indigo-500/30 transition-all disabled:opacity-50 disabled:cursor-not-allowed text-sm">
          <i :class="saving ? 'fas fa-spinner animate-spin' : 'fas fa-save'" class="mr-2"></i>
          {{ saving ? '保存中...' : '保存设置' }}
        </button>
      </div>
    </div>

    <!-- 加载状态 -->
    <div v-if="loading" class="fixed inset-0 bg-black/20 backdrop-blur-sm flex items-center justify-center z-50">
      <div class="bg-white dark:bg-slate-800 rounded-2xl p-6 shadow-xl text-center">
        <i class="fas fa-spinner animate-spin text-2xl text-indigo-500 mb-2"></i>
        <p class="text-slate-600 dark:text-white/70 text-sm">加载中...</p>
      </div>
    </div>
  </div>
</template>
