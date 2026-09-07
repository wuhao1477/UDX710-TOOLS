<script setup>
import { ref, computed, onMounted, onUnmounted, inject } from 'vue'
import { useI18n } from 'vue-i18n'
import { getChargeConfig, setChargeConfig, chargeOn, chargeOff } from '../composables/useApi'
import { useToast } from '../composables/useToast'

const { t } = useI18n()
const { success, error: showError } = useToast()
const capabilities = inject('capabilities', ref(null))
const hasBattery = computed(() => capabilities.value?.power?.battery_supported !== false)

const batteryStatus = ref({ level: 0, charging: false, health: '-', temperature: 0, voltage: 0, current: 0 })
const firmwarePower = ref({ voltage: null, current: null, temperatureRaw: null, firmwareBattery: null, charging: null, status: '', source: '' })
const chargeConfig = ref({ enabled: false, startLevel: 20, stopLevel: 80 })
const saving = ref(false)
const loading = ref(true)
const manualControlling = ref(false)

const presets = computed(() => [
  { name: t('battery.longevityMode'), start: 20, stop: 80, desc: t('battery.longevityDesc'), icon: 'fa-heart', gradient: 'from-green-500 to-emerald-400' },
  { name: t('battery.balancedMode'), start: 15, stop: 90, desc: t('battery.balancedDesc'), icon: 'fa-balance-scale', gradient: 'from-blue-500 to-cyan-400' },
  { name: t('battery.fullChargeMode'), start: 10, stop: 100, desc: t('battery.fullChargeDesc'), icon: 'fa-bolt', gradient: 'from-amber-500 to-orange-400' }
])

const batteryGradient = computed(() => {
  const level = batteryStatus.value.level
  if (level <= 20) return 'from-red-500 via-red-400 to-orange-500'
  if (level <= 50) return 'from-amber-500 via-yellow-400 to-lime-500'
  return 'from-green-500 via-emerald-400 to-cyan-500'
})

const healthStatus = computed(() => {
  const h = batteryStatus.value.health
  if (typeof h === 'string') {
    const map = { 
      'Good': { text: t('battery.healthGood'), color: 'text-green-400', icon: 'fa-heart' }, 
      'Overheat': { text: t('battery.healthOverheat'), color: 'text-red-400', icon: 'fa-fire' },
      'Dead': { text: t('battery.healthDead'), color: 'text-red-400', icon: 'fa-skull' }, 
      'Over voltage': { text: t('battery.healthOverVoltage'), color: 'text-amber-400', icon: 'fa-bolt' },
      'Cold': { text: t('battery.healthCold'), color: 'text-blue-400', icon: 'fa-snowflake' }, 
      'Unknown': { text: t('battery.healthUnknown'), color: 'text-white/50', icon: 'fa-question' } 
    }
    return map[h] || { text: h, color: 'text-green-400', icon: 'fa-heart' }
  }
  if (h >= 90) return { text: t('battery.healthExcellent'), color: 'text-green-400', icon: 'fa-heart' }
  if (h >= 80) return { text: t('battery.healthGood'), color: 'text-blue-400', icon: 'fa-thumbs-up' }
  if (h >= 60) return { text: t('battery.healthFair'), color: 'text-amber-400', icon: 'fa-exclamation' }
  return { text: t('battery.healthPoor'), color: 'text-red-400', icon: 'fa-triangle-exclamation' }
})

function applyPreset(preset) { chargeConfig.value.startLevel = preset.start; chargeConfig.value.stopLevel = preset.stop }

async function saveConfig() {
  if (chargeConfig.value.enabled && chargeConfig.value.startLevel >= chargeConfig.value.stopLevel) { showError(t('battery.startMustLessThanStop')); return }
  saving.value = true
  try {
    const res = await setChargeConfig(chargeConfig.value.enabled, chargeConfig.value.startLevel, chargeConfig.value.stopLevel)
    if (res.Code === 0) success(t('battery.settingsSaved')); else showError(t('battery.saveFailed') + ': ' + res.Error)
  } catch (err) { showError(t('battery.saveFailed') + ': ' + err.message) }
  finally { saving.value = false }
}

async function toggleCharging() {
  manualControlling.value = true
  try {
    const res = batteryStatus.value.charging ? await chargeOff() : await chargeOn()
    if (res.Code === 0) await fetchData(); else showError(t('battery.operationFailed') + ': ' + res.Error)
  } catch (err) { showError(t('battery.operationFailed') + ': ' + err.message) }
  finally { manualControlling.value = false }
}

async function fetchData() {
  try {
    const res = await getChargeConfig()
    if (res.Code === 0 && res.Data) {
      const { config, battery } = res.Data
      if (config.supported) {
        chargeConfig.value = { enabled: config.enabled, startLevel: config.startThreshold, stopLevel: config.stopThreshold }
        batteryStatus.value = {
          level: battery.capacity, charging: battery.charging, health: battery.health || '-',
          temperature: battery.temperature || 0, voltage: battery.voltage?.toFixed(2) || 0, current: Math.abs(battery.current || 0).toFixed(2)
        }
      } else {
        firmwarePower.value = battery
      }
    }
  } catch (error) { console.error('获取充电配置失败:', error) }
  finally { loading.value = false }
}

let refreshTimer = null
onMounted(() => { fetchData(); refreshTimer = setInterval(fetchData, 30000) })
onUnmounted(() => { if (refreshTimer) clearInterval(refreshTimer) })
</script>

<template>
  <div class="space-y-6">
    <div v-if="capabilities && !hasBattery" class="rounded-3xl bg-white/95 dark:bg-white/5 border border-slate-200/60 dark:border-white/10 p-8 text-center">
      <i class="fas fa-plug text-4xl text-blue-400 mb-4"></i>
      <h2 class="text-xl font-bold text-slate-900 dark:text-white">外部供电设备</h2>
      <p class="mt-2 text-slate-500 dark:text-white/50">当前设备没有内置电池，充电控制不适用。</p>
      <div class="mt-5 grid grid-cols-2 gap-3 text-left" v-if="firmwarePower.source === 'fyapp'">
        <div class="rounded-xl bg-slate-100 dark:bg-white/5 p-3">
          <p class="text-xs text-slate-500 dark:text-white/50">固件电压</p>
          <p class="font-semibold text-slate-900 dark:text-white">{{ firmwarePower.voltage ?? '--' }} V</p>
        </div>
        <div class="rounded-xl bg-slate-100 dark:bg-white/5 p-3">
          <p class="text-xs text-slate-500 dark:text-white/50">固件电流</p>
          <p class="font-semibold text-slate-900 dark:text-white">{{ firmwarePower.current ?? '--' }} A</p>
        </div>
        <div class="rounded-xl bg-slate-100 dark:bg-white/5 p-3">
          <p class="text-xs text-slate-500 dark:text-white/50">固件温度原始值</p>
          <p class="font-semibold text-slate-900 dark:text-white">{{ firmwarePower.temperatureRaw ?? '--' }}</p>
        </div>
        <div class="rounded-xl bg-slate-100 dark:bg-white/5 p-3">
          <p class="text-xs text-slate-500 dark:text-white/50">固件电量字段</p>
          <p class="font-semibold text-slate-900 dark:text-white">{{ firmwarePower.firmwareBattery ?? '--' }}</p>
        </div>
        <div class="rounded-xl bg-slate-100 dark:bg-white/5 p-3">
          <p class="text-xs text-slate-500 dark:text-white/50">固件充电状态</p>
          <p class="font-semibold text-slate-900 dark:text-white">{{ firmwarePower.status || '--' }}</p>
        </div>
      </div>
      <p v-if="firmwarePower.source === 'fyapp'" class="mt-3 text-xs text-amber-500">以上是 fyapp 固件上报值，不代表内置电池或真实充电功率。</p>
    </div>

    <div v-else-if="loading" class="flex items-center justify-center py-20">
      <div class="relative">
        <div class="w-20 h-20 border-4 border-green-500/30 border-t-green-500 rounded-full animate-spin"></div>
        <div class="absolute inset-0 flex items-center justify-center"><i class="fas fa-battery-half text-green-400 text-2xl"></i></div>
      </div>
    </div>

    <template v-else>
    <!-- 电池状态大卡片 -->
    <div class="relative overflow-hidden rounded-3xl bg-gradient-to-br from-slate-100 via-slate-50 to-slate-100 dark:from-slate-800/80 dark:via-slate-900/90 dark:to-slate-800/80 border border-slate-200/60 dark:border-white/10 p-8 shadow-lg shadow-slate-200/50 dark:shadow-black/20 hover:shadow-xl hover:shadow-slate-300/60 dark:hover:shadow-black/30 transition-all duration-300">
      <!-- 动态背景 -->
      <div class="absolute top-0 right-0 w-96 h-96 bg-green-500/5 rounded-full blur-3xl -translate-y-1/2 translate-x-1/2"></div>
      <div class="absolute bottom-0 left-0 w-64 h-64 bg-emerald-500/5 rounded-full blur-3xl translate-y-1/2 -translate-x-1/2"></div>
      
      <div class="relative flex flex-col lg:flex-row items-center gap-8">
        <!-- 3D电池图标 -->
        <div class="relative flex-shrink-0">
          <div class="relative w-48 h-28 perspective-1000">
            <!-- 电池主体 -->
            <div class="absolute inset-0 bg-gradient-to-b from-slate-300 to-slate-400 dark:from-slate-700 dark:to-slate-800 rounded-2xl border-4 border-slate-400 dark:border-slate-600 shadow-2xl overflow-hidden">
              <!-- 电量填充 -->
              <div class="absolute bottom-0 left-0 right-0 transition-all duration-1000 bg-gradient-to-t" :class="batteryGradient" :style="{ height: batteryStatus.level + '%' }">
                <div class="absolute inset-0 bg-gradient-to-r from-white/0 via-white/20 to-white/0 animate-pulse"></div>
                <!-- 波浪效果 -->
                <div class="absolute top-0 left-0 right-0 h-2 bg-white/30 rounded-full blur-sm"></div>
              </div>
              <!-- 电量刻度 -->
              <div class="absolute inset-0 flex flex-col justify-between py-2 px-3">
                <div class="h-px bg-white/10"></div><div class="h-px bg-white/10"></div><div class="h-px bg-white/10"></div><div class="h-px bg-white/10"></div>
              </div>
              <!-- 电量数字 -->
              <div class="absolute inset-0 flex items-center justify-center">
                <span class="text-slate-900 dark:text-white text-4xl font-black drop-shadow-lg">{{ batteryStatus.level }}%</span>
              </div>
            </div>
            <!-- 电池头 -->
            <div class="absolute -right-3 top-1/2 -translate-y-1/2 w-3 h-10 bg-gradient-to-r from-slate-400 to-slate-500 dark:from-slate-600 dark:to-slate-700 rounded-r-lg"></div>
          </div>
          <!-- 充电闪电 -->
          <div v-if="batteryStatus.charging" class="absolute top-1/2 left-1/2 -translate-x-1/2 -translate-y-1/2 z-10">
            <div class="relative">
              <i class="fas fa-bolt text-amber-400 text-3xl animate-pulse drop-shadow-lg"></i>
              <div class="absolute inset-0 blur-md bg-amber-400/50 rounded-full"></div>
            </div>
          </div>
        </div>

    
    <!-- 电池详情 -->
        <div class="flex-1 grid grid-cols-2 lg:grid-cols-4 gap-4 w-full">
          <!-- 充电状态 -->
          <div class="group relative overflow-hidden p-4 bg-white dark:bg-white/5 backdrop-blur rounded-2xl border border-slate-200 dark:border-white/10 hover:border-amber-500/50 transition-all">
            <div class="absolute -top-4 -right-4 w-16 h-16 bg-amber-500/10 rounded-full blur-xl group-hover:scale-150 transition-transform"></div>
            <div class="relative">
              <div class="flex items-center justify-between mb-2">
                <div class="w-10 h-10 rounded-xl bg-gradient-to-br from-amber-500 to-orange-400 flex items-center justify-center shadow-lg shadow-amber-500/30">
                  <i class="fas fa-plug text-white"></i>
                </div>
                <button @click="toggleCharging" :disabled="manualControlling"
                  class="px-3 py-1.5 text-xs rounded-lg transition-all" :class="batteryStatus.charging ? 'bg-red-500/20 text-red-400 hover:bg-red-500/30' : 'bg-green-500/20 text-green-400 hover:bg-green-500/30'">
                  <i :class="manualControlling ? 'fas fa-spinner animate-spin' : (batteryStatus.charging ? 'fas fa-stop' : 'fas fa-play')"></i>
                </button>
              </div>
              <p class="text-slate-500 dark:text-white/50 text-xs">{{ t('battery.chargingStatus') }}</p>
              <p class="text-slate-900 dark:text-white font-bold text-lg">{{ batteryStatus.charging ? t('battery.charging') : t('battery.notCharging') }}</p>
            </div>
          </div>
          
          <!-- 电池健康 -->
          <div class="group relative overflow-hidden p-4 bg-white dark:bg-white/5 backdrop-blur rounded-2xl border border-slate-200 dark:border-white/10 hover:border-green-500/50 transition-all">
            <div class="absolute -top-4 -right-4 w-16 h-16 bg-green-500/10 rounded-full blur-xl group-hover:scale-150 transition-transform"></div>
            <div class="relative">
              <div class="w-10 h-10 rounded-xl bg-gradient-to-br from-green-500 to-emerald-400 flex items-center justify-center shadow-lg shadow-green-500/30 mb-2">
                <i :class="['fas', healthStatus.icon, 'text-white']"></i>
              </div>
              <p class="text-slate-500 dark:text-white/50 text-xs">{{ t('battery.health') }}</p>
              <p class="font-bold text-lg" :class="healthStatus.color">{{ healthStatus.text }}</p>
            </div>
          </div>
          
          <!-- 温度 -->
          <div class="group relative overflow-hidden p-4 bg-white dark:bg-white/5 backdrop-blur rounded-2xl border border-slate-200 dark:border-white/10 hover:border-cyan-500/50 transition-all">
            <div class="absolute -top-4 -right-4 w-16 h-16 bg-cyan-500/10 rounded-full blur-xl group-hover:scale-150 transition-transform"></div>
            <div class="relative">
              <div class="w-10 h-10 rounded-xl bg-gradient-to-br from-cyan-500 to-blue-400 flex items-center justify-center shadow-lg shadow-cyan-500/30 mb-2">
                <i class="fas fa-temperature-half text-white"></i>
              </div>
              <p class="text-slate-500 dark:text-white/50 text-xs">{{ t('battery.temperature') }}</p>
              <p class="text-slate-900 dark:text-white font-bold text-lg">{{ batteryStatus.temperature }}°C</p>
            </div>
          </div>
          
          <!-- 电压电流 -->
          <div class="group relative overflow-hidden p-4 bg-white dark:bg-white/5 backdrop-blur rounded-2xl border border-slate-200 dark:border-white/10 hover:border-purple-500/50 transition-all">
            <div class="absolute -top-4 -right-4 w-16 h-16 bg-purple-500/10 rounded-full blur-xl group-hover:scale-150 transition-transform"></div>
            <div class="relative">
              <div class="w-10 h-10 rounded-xl bg-gradient-to-br from-purple-500 to-pink-400 flex items-center justify-center shadow-lg shadow-purple-500/30 mb-2">
                <i class="fas fa-bolt text-white"></i>
              </div>
              <p class="text-slate-500 dark:text-white/50 text-xs">{{ t('battery.voltageCurrent') }}</p>
              <p class="text-slate-900 dark:text-white font-bold">{{ batteryStatus.voltage }}V / {{ batteryStatus.current }}A</p>
            </div>
          </div>
        </div>
      </div>
    </div>


    <!-- 快捷预设 -->
    <div class="grid grid-cols-1 md:grid-cols-3 gap-4">
      <button v-for="preset in presets" :key="preset.name" @click="applyPreset(preset)"
        class="group relative overflow-hidden p-5 rounded-2xl bg-white/95 dark:bg-white/5 backdrop-blur border border-slate-200/60 dark:border-white/10 shadow-lg shadow-slate-200/40 dark:shadow-black/20 hover:shadow-xl hover:shadow-slate-300/50 dark:hover:shadow-black/30 hover:border-slate-300 dark:hover:border-white/30 hover:-translate-y-1 transition-all duration-300">
        <div class="absolute inset-0 bg-gradient-to-br opacity-0 group-hover:opacity-10 transition-opacity" :class="preset.gradient"></div>
        <div class="relative flex items-center space-x-4">
          <div class="w-14 h-14 rounded-2xl bg-gradient-to-br flex items-center justify-center shadow-lg" :class="preset.gradient">
            <i :class="['fas', preset.icon, 'text-white text-xl']"></i>
          </div>
          <div class="flex-1 text-left">
            <p class="text-slate-900 dark:text-white font-bold text-lg">{{ preset.name }}</p>
            <p class="text-slate-500 dark:text-white/50 text-sm">{{ preset.desc }}</p>
            <p class="text-slate-400 dark:text-white/40 text-xs mt-1">{{ preset.start }}% - {{ preset.stop }}%</p>
          </div>
          <i class="fas fa-chevron-right text-slate-300 dark:text-white/30 group-hover:text-slate-500 dark:group-hover:text-white/60 group-hover:translate-x-1 transition-all"></i>
        </div>
      </button>
    </div>

    <!-- 充电控制设置 -->
    <div class="relative overflow-hidden rounded-3xl bg-gradient-to-br from-emerald-100 via-slate-50 to-green-100 dark:from-emerald-900/20 dark:via-slate-900/50 dark:to-green-900/20 border border-slate-200/60 dark:border-white/10 p-6 shadow-lg shadow-emerald-200/50 dark:shadow-black/20 hover:shadow-xl hover:shadow-emerald-300/60 dark:hover:shadow-black/30 transition-all duration-300">
      <div class="absolute top-0 left-0 w-72 h-72 bg-emerald-500/10 rounded-full blur-3xl"></div>
      <div class="absolute bottom-0 right-0 w-64 h-64 bg-green-500/10 rounded-full blur-3xl animate-pulse"></div>
      
      <div class="relative">
        <div class="flex items-center justify-between mb-8">
          <div class="flex items-center space-x-4">
            <div class="w-14 h-14 rounded-2xl bg-gradient-to-br from-emerald-500 to-green-400 flex items-center justify-center shadow-lg shadow-emerald-500/30">
              <i class="fas fa-sliders text-white text-2xl"></i>
            </div>
            <div>
              <h3 class="text-slate-900 dark:text-white font-bold text-xl">{{ t('battery.chargeControl') }}</h3>
              <p class="text-slate-500 dark:text-white/50 text-sm">{{ t('battery.subtitle') }}</p>
            </div>
          </div>
          <label class="relative cursor-pointer">
            <input type="checkbox" v-model="chargeConfig.enabled" class="sr-only peer">
            <div class="w-16 h-8 bg-slate-200 dark:bg-white/20 rounded-full peer peer-checked:bg-gradient-to-r peer-checked:from-emerald-500 peer-checked:to-green-400 transition-all shadow-inner"></div>
            <div class="absolute top-1 left-1 w-6 h-6 bg-white rounded-full shadow-lg transition-transform peer-checked:translate-x-8"></div>
          </label>
        </div>

      
  <div :class="{ 'opacity-40 pointer-events-none': !chargeConfig.enabled }" class="transition-opacity">
          <!-- 可视化范围条 -->
          <div class="relative h-20 bg-slate-100 dark:bg-white/5 rounded-2xl overflow-hidden mb-8 border border-slate-200 dark:border-white/10">
            <!-- 背景刻度 -->
            <div class="absolute inset-0 flex">
              <div v-for="i in 10" :key="i" class="flex-1 border-r border-slate-200 dark:border-white/5 last:border-r-0 flex items-end justify-center pb-1">
                <span class="text-slate-300 dark:text-white/20 text-xs">{{ i * 10 }}</span>
              </div>
            </div>
            <!-- 充电范围 -->
            <div class="absolute top-2 bottom-2 bg-gradient-to-r from-emerald-500/30 via-green-500/40 to-emerald-500/30 rounded-xl border-2 border-green-400/50 transition-all duration-300"
              :style="{ left: chargeConfig.startLevel + '%', right: (100 - chargeConfig.stopLevel) + '%' }">
              <div class="absolute inset-0 bg-gradient-to-r from-white/0 via-white/10 to-white/0 animate-pulse"></div>
              <!-- 起始标记 -->
              <div class="absolute left-0 top-1/2 -translate-y-1/2 -translate-x-1/2 w-6 h-6 bg-green-500 rounded-full border-4 border-slate-900 shadow-lg"></div>
              <!-- 结束标记 -->
              <div class="absolute right-0 top-1/2 -translate-y-1/2 translate-x-1/2 w-6 h-6 bg-red-500 rounded-full border-4 border-slate-900 shadow-lg"></div>
            </div>
            <!-- 当前电量指示 -->
            <div class="absolute top-0 bottom-0 w-0.5 bg-slate-900 dark:bg-white shadow-lg transition-all duration-500" :style="{ left: batteryStatus.level + '%' }">
              <div class="absolute -top-1 left-1/2 -translate-x-1/2 px-2 py-0.5 bg-slate-900 dark:bg-white text-white dark:text-slate-900 text-xs font-bold rounded">{{ batteryStatus.level }}%</div>
              <div class="absolute -bottom-1 left-1/2 -translate-x-1/2 w-0 h-0 border-l-4 border-r-4 border-t-4 border-transparent border-t-slate-900 dark:border-t-white"></div>
            </div>
          </div>

          <!-- 滑块控制 -->
          <div class="grid grid-cols-1 md:grid-cols-2 gap-8 mb-8">
            <div class="p-5 bg-white dark:bg-white/5 rounded-2xl border border-slate-200 dark:border-white/10">
              <div class="flex items-center justify-between mb-4">
                <div class="flex items-center space-x-3">
                  <div class="w-10 h-10 rounded-xl bg-gradient-to-br from-green-500 to-emerald-400 flex items-center justify-center">
                    <i class="fas fa-play text-white"></i>
                  </div>
                  <span class="text-slate-600 dark:text-white/70">{{ t('battery.startChargeBelow') }}</span>
                </div>
                <span class="text-green-600 dark:text-green-400 font-bold text-2xl">{{ chargeConfig.startLevel }}%</span>
              </div>
              <input type="range" v-model.number="chargeConfig.startLevel" min="5" max="95" step="5"
                class="w-full h-3 bg-slate-200 dark:bg-white/10 rounded-full appearance-none cursor-pointer accent-green-500">
              <div class="flex justify-between text-xs text-slate-400 dark:text-white/30 mt-2"><span>5%</span><span>95%</span></div>
            </div>

            <div class="p-5 bg-white dark:bg-white/5 rounded-2xl border border-slate-200 dark:border-white/10">
              <div class="flex items-center justify-between mb-4">
                <div class="flex items-center space-x-3">
                  <div class="w-10 h-10 rounded-xl bg-gradient-to-br from-red-500 to-orange-400 flex items-center justify-center">
                    <i class="fas fa-stop text-white"></i>
                  </div>
                  <span class="text-slate-600 dark:text-white/70">{{ t('battery.stopChargeAbove') }}</span>
                </div>
                <span class="text-red-600 dark:text-red-400 font-bold text-2xl">{{ chargeConfig.stopLevel }}%</span>
              </div>
              <input type="range" v-model.number="chargeConfig.stopLevel" min="10" max="100" step="5"
                class="w-full h-3 bg-slate-200 dark:bg-white/10 rounded-full appearance-none cursor-pointer accent-red-500">
              <div class="flex justify-between text-xs text-slate-400 dark:text-white/30 mt-2"><span>10%</span><span>100%</span></div>
            </div>
          </div>

   
       <!-- 说明卡片 -->
          <div class="p-5 bg-gradient-to-r from-emerald-500/10 via-green-500/5 to-emerald-500/10 rounded-2xl border border-emerald-500/20 mb-6">
            <div class="flex items-start space-x-4">
              <div class="w-12 h-12 rounded-xl bg-gradient-to-br from-emerald-500 to-green-400 flex items-center justify-center flex-shrink-0">
                <i class="fas fa-lightbulb text-white text-xl"></i>
              </div>
              <div>
                <p class="text-emerald-600 dark:text-emerald-400 font-bold mb-1">{{ t('battery.strategyExplain') }}</p>
                <p class="text-slate-600 dark:text-white/60 text-sm leading-relaxed">
                  {{ t('battery.strategyDesc', { start: chargeConfig.startLevel, stop: chargeConfig.stopLevel }) }}
                </p>
              </div>
            </div>
          </div>

        </div>

        <!-- 保存按钮 - 始终可用 -->
        <button @click="saveConfig" :disabled="saving"
          class="w-full py-4 bg-gradient-to-r from-emerald-500 via-green-500 to-emerald-500 text-white font-bold text-lg rounded-2xl hover:shadow-lg hover:shadow-emerald-500/30 transition-all disabled:opacity-50 relative overflow-hidden group mt-6">
          <div class="absolute inset-0 bg-gradient-to-r from-white/0 via-white/20 to-white/0 -translate-x-full group-hover:translate-x-full transition-transform duration-1000"></div>
          <span class="relative">
            <i :class="saving ? 'fas fa-spinner animate-spin' : 'fas fa-save'" class="mr-2"></i>
            {{ saving ? t('battery.saving') : t('battery.saveSettings') }}
          </span>
        </button>
      </div>
    </div>
    </template>
  </div>
</template>

<style scoped>
input[type="range"]::-webkit-slider-thumb {
  -webkit-appearance: none;
  width: 24px;
  height: 24px;
  background: white;
  border-radius: 50%;
  cursor: pointer;
  box-shadow: 0 4px 12px rgba(0,0,0,0.3);
  transition: transform 0.2s;
}
input[type="range"]::-webkit-slider-thumb:hover {
  transform: scale(1.2);
}
</style>
