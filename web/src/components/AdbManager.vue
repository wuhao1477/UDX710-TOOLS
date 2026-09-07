<script setup>
import { ref, onMounted } from 'vue'
import { getAdbStatus, setAdbWireless, setAdbUsb, restartAdb } from '../composables/useApi'
import { useToast } from '../composables/useToast'
import { useConfirm } from '../composables/useConfirm'

const { success, error } = useToast()
const { confirm } = useConfirm()
const status = ref({ daemonRunning: false, usbEnabled: false, wirelessEnabled: false, wirelessPort: 5555 })
const loading = ref(true)
const busy = ref(null)

async function loadStatus() {
  loading.value = true
  try {
    const response = await getAdbStatus()
    status.value = response.Data || response.data || status.value
  } catch (err) {
    error(err.message || '读取 ADB 状态失败')
  } finally {
    loading.value = false
  }
}

async function changeWireless(enabled) {
  if (!await confirm({
    title: enabled ? '开启无线 ADB' : '关闭无线 ADB',
    message: enabled ? '设备将监听 5555 端口。' : '关闭后只能通过 USB ADB 管理设备。'
  })) return
  busy.value = 'wireless'
  try {
    await setAdbWireless(enabled)
    success('无线 ADB 设置请求已发送')
    setTimeout(loadStatus, 1000)
  } catch (err) { error(err.message || '无线 ADB 设置失败') }
  finally { busy.value = null }
}

async function changeUsb(enabled) {
  if (!await confirm({
    title: enabled ? '开启 USB ADB' : '关闭 USB ADB',
    message: 'USB ADB 操作会重新枚举 Type-C，当前 USB 管理连接可能中断。'
  })) return
  busy.value = 'usb'
  try {
    await setAdbUsb(enabled)
    success('USB ADB 设置请求已发送')
    setTimeout(loadStatus, 1500)
  } catch (err) { error(err.message || 'USB ADB 设置失败') }
  finally { busy.value = null }
}

async function handleRestart() {
  if (!await confirm({ title: '重启 ADB', message: '当前 ADB 连接会短暂中断。' })) return
  busy.value = 'restart'
  try {
    await restartAdb()
    success('ADB 重启请求已发送')
    setTimeout(loadStatus, 1000)
  } catch (err) { error(err.message || 'ADB 重启失败') }
  finally { busy.value = null }
}

onMounted(loadStatus)
</script>

<template>
  <div class="space-y-4">
    <div class="flex items-center justify-between">
      <div>
        <h2 class="text-xl font-bold text-slate-900 dark:text-white">ADB 管理</h2>
        <p class="text-sm text-slate-500 dark:text-white/50">控制当前设备支持的 USB 和无线 ADB。</p>
      </div>
      <button @click="loadStatus" :disabled="loading" class="px-3 py-2 rounded-xl bg-slate-200 dark:bg-white/10 text-slate-700 dark:text-white/80">
        <i class="fas fa-sync-alt mr-1" :class="{ 'animate-spin': loading }"></i>刷新
      </button>
    </div>

    <div class="rounded-2xl bg-white/95 dark:bg-white/5 border border-slate-200/60 dark:border-white/10 p-5">
      <div class="flex items-center justify-between">
        <span class="text-slate-600 dark:text-white/70">adbd 守护进程</span>
        <span :class="status.daemonRunning ? 'text-green-500' : 'text-slate-400'">{{ status.daemonRunning ? '运行中' : '未运行' }}</span>
      </div>
      <div class="mt-4 grid gap-3 md:grid-cols-2">
        <div class="rounded-xl bg-slate-100 dark:bg-white/5 p-4">
          <p class="text-sm text-slate-500 dark:text-white/50">USB ADB</p>
          <p class="mt-1 font-semibold text-slate-900 dark:text-white">{{ status.usbEnabled ? '已启用' : '已关闭' }}</p>
          <button @click="changeUsb(!status.usbEnabled)" :disabled="busy" class="mt-3 px-3 py-2 rounded-lg bg-blue-500 text-white disabled:opacity-50">
            {{ status.usbEnabled ? '关闭 USB ADB' : '开启 USB ADB' }}
          </button>
        </div>
        <div class="rounded-xl bg-slate-100 dark:bg-white/5 p-4">
          <p class="text-sm text-slate-500 dark:text-white/50">无线 ADB</p>
          <p class="mt-1 font-semibold text-slate-900 dark:text-white">{{ status.wirelessEnabled ? `已启用 :${status.wirelessPort}` : '已关闭' }}</p>
          <button @click="changeWireless(!status.wirelessEnabled)" :disabled="busy" class="mt-3 px-3 py-2 rounded-lg bg-purple-500 text-white disabled:opacity-50">
            {{ status.wirelessEnabled ? '关闭无线 ADB' : '开启无线 ADB' }}
          </button>
        </div>
      </div>
      <button @click="handleRestart" :disabled="busy" class="mt-4 w-full px-3 py-2 rounded-lg bg-slate-700 text-white disabled:opacity-50">
        重启 ADB
      </button>
    </div>
  </div>
</template>
