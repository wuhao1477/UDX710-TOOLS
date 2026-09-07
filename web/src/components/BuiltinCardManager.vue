<script setup>
import { ref, onMounted } from 'vue'
import { getBuiltinCards, checkBuiltinCardRealName, approveBuiltinCardSwitch, switchBuiltinCardGoform } from '../composables/useApi'
import { useToast } from '../composables/useToast'
import { useConfirm } from '../composables/useConfirm'

const { success, error } = useToast()
const { confirm } = useConfirm()
const cards = ref([])
const currentPriority = ref(null)
const loading = ref(true)
const busy = ref(null)
const unsupported = ref(false)
const realNameStatus = ref({})

function responseData(response) {
  return response?.Data || response?.data || {}
}

async function loadCards() {
  loading.value = true
  unsupported.value = false
  try {
    const response = await getBuiltinCards()
    const data = responseData(response)
    cards.value = data.cards || []
    currentPriority.value = data.currentPriority ?? null
  } catch (err) {
    unsupported.value = true
    cards.value = []
    error(err.message || '当前设备不支持内置卡运营商管理')
  } finally {
    loading.value = false
  }
}

async function checkRealName(card) {
  busy.value = `realname:${card.operatorId}`
  try {
    const response = await checkBuiltinCardRealName(card.operatorId)
    const data = responseData(response)
    realNameStatus.value[card.operatorId] = data.realNameStatus
    if (response.Code === 0 && data.verified) success('实名状态：已实名')
    else error('该内置卡尚未实名')
  } catch (err) {
    error(err.message || '实名状态查询失败')
  } finally {
    busy.value = null
  }
}

async function switchCard(card) {
  if (!await confirm({
    title: '确认切换运营商',
    message: `将切换到${card.operatorName}，设备会短暂断网并重新注册。`
  })) return

  busy.value = `switch:${card.operatorId}`
  try {
    const approval = await approveBuiltinCardSwitch(card.operatorId)
    const data = responseData(approval)
    if (approval.Code !== 0 || !data.goformQuery) {
      throw new Error(approval.Error || '实名校验未通过')
    }
    await switchBuiltinCardGoform(data.goformQuery)
    currentPriority.value = data.priorityMnc
    success(`已请求切换到${card.operatorName}`)
    setTimeout(loadCards, 1500)
  } catch (err) {
    error(err.message || '运营商切换失败')
  } finally {
    busy.value = null
  }
}

onMounted(loadCards)
</script>

<template>
  <div class="space-y-4">
    <div class="flex items-center justify-between">
      <div>
        <h2 class="text-xl font-bold text-slate-900 dark:text-white">内置卡运营商</h2>
        <p class="text-sm text-slate-500 dark:text-white/50">使用设备原生 goform 切换，不改写卡文件。</p>
      </div>
      <button @click="loadCards" :disabled="loading" class="px-3 py-2 rounded-xl bg-slate-200 dark:bg-white/10 text-slate-700 dark:text-white/80">
        <i class="fas fa-sync-alt mr-1" :class="{ 'animate-spin': loading }"></i>刷新
      </button>
    </div>

    <div v-if="unsupported" class="rounded-2xl border border-amber-500/30 bg-amber-500/10 p-5 text-amber-500">
      当前设备不是已确认的 SZ50 设备，未启用内置卡运营商切换。
    </div>

    <div v-else-if="loading" class="py-16 text-center text-slate-500 dark:text-white/50">正在读取内置卡...</div>

    <div v-else class="grid gap-4 md:grid-cols-2">
      <div v-for="card in cards" :key="card.slot" class="rounded-2xl bg-white/95 dark:bg-white/5 border border-slate-200/60 dark:border-white/10 p-5">
        <div class="flex items-start justify-between gap-3">
          <div>
            <p class="text-lg font-bold text-slate-900 dark:text-white">{{ card.operatorName }}</p>
            <p class="text-sm text-slate-500 dark:text-white/50">{{ card.operatorId }} · {{ card.slot }}</p>
          </div>
          <span class="px-2 py-1 rounded-lg text-xs" :class="card.realState === 2 ? 'bg-green-500/10 text-green-500' : 'bg-slate-500/10 text-slate-500'">
            {{ card.realState === 2 ? '卡已就绪' : '卡不可用' }}
          </span>
        </div>
        <p class="mt-3 text-sm text-slate-500 dark:text-white/50">ICCID：{{ card.iccid }}</p>
        <p v-if="realNameStatus[card.operatorId] !== undefined" class="mt-1 text-xs" :class="realNameStatus[card.operatorId] === 2 ? 'text-green-500' : 'text-amber-500'">
          实名状态：{{ realNameStatus[card.operatorId] === 2 ? '已实名' : '未实名' }}
        </p>
        <div class="mt-4 flex gap-2">
          <button @click="checkRealName(card)" :disabled="busy" class="flex-1 px-3 py-2 rounded-xl bg-slate-100 dark:bg-white/10 text-slate-700 dark:text-white/80 disabled:opacity-50">
            查询实名
          </button>
          <button @click="switchCard(card)" :disabled="busy || card.realState !== 2" class="flex-1 px-3 py-2 rounded-xl bg-blue-500 text-white disabled:opacity-50">
            切换
          </button>
        </div>
      </div>
    </div>

    <p v-if="currentPriority !== null" class="text-xs text-slate-400">当前设备优先级：{{ currentPriority }}</p>
  </div>
</template>
