<script setup>
import { computed, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { checkBuiltinCardRealName, getBuiltinCards, switchBuiltinCard } from '../composables/useApi'
import { useConfirm } from '../composables/useConfirm'
import { useToast } from '../composables/useToast'

const props = defineProps({ compact: { type: Boolean, default: false } })
const { t } = useI18n()
const { confirm } = useConfirm()
const { success, error } = useToast()

const cards = ref([])
const currentPriority = ref(null)
const realNameStatus = ref({})
const loading = ref(true)
const busy = ref(null)
const unsupported = ref(false)

const priorityCarrier = { 7: '中国移动', 9: '中国电信', 11: '中国联通' }
const currentCarrier = computed(() => priorityCarrier[currentPriority.value] || t('builtinCard.unknownCarrier'))

function responseData(response) {
  return response?.Data || response?.data || {}
}

async function loadCards(showError = false) {
  loading.value = true
  unsupported.value = false
  try {
    const response = await getBuiltinCards()
    const data = responseData(response)
    cards.value = data.cards || []
    currentPriority.value = data.currentPriority ?? null
  } catch (err) {
    cards.value = []
    currentPriority.value = null
    unsupported.value = err.status === 409
    if (showError && !unsupported.value) error(err.message || t('builtinCard.loadFailed'))
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
    if (response.Code === 0 && data.verified) success(t('builtinCard.verified'))
    else error(t('builtinCard.notVerified'))
  } catch (err) {
    error(err.message || t('builtinCard.realNameFailed'))
  } finally {
    busy.value = null
  }
}

async function switchCard(card) {
  if (!await confirm({
    title: t('builtinCard.confirmTitle'),
    message: t('builtinCard.confirmMessage', { carrier: card.operatorName })
  })) return

  busy.value = `switch:${card.operatorId}`
  try {
    const response = await switchBuiltinCard(card.operatorId)
    const data = responseData(response)
    if (response.Code !== 0 || data.result !== 0) {
      throw new Error(response.Error || t('builtinCard.switchFailed'))
    }
    currentPriority.value = data.priorityMnc
    success(t('builtinCard.switched', { carrier: card.operatorName }))
    setTimeout(() => loadCards(), 1500)
  } catch (err) {
    error(err.message || t('builtinCard.switchFailed'))
  } finally {
    busy.value = null
  }
}

onMounted(() => loadCards())
</script>

<template>
  <section :class="props.compact ? 'rounded-2xl bg-white/95 dark:bg-white/5 border border-slate-200/60 dark:border-white/10 p-5' : 'space-y-4'">
    <div class="flex items-center justify-between gap-4">
      <div>
        <div class="flex items-center gap-2">
          <i class="fas fa-sim-card text-purple-400"></i>
          <h3 class="text-base font-bold text-slate-900 dark:text-white">{{ t('builtinCard.title') }}</h3>
        </div>
        <p class="mt-1 text-sm text-slate-500 dark:text-white/50">{{ t('builtinCard.description') }}</p>
      </div>
      <div class="flex items-center gap-2">
        <span v-if="currentPriority !== null" class="hidden sm:inline-flex items-center gap-1 px-3 py-1.5 rounded-full bg-green-500/10 text-green-500 text-sm">
          <i class="fas fa-signal"></i>{{ t('builtinCard.current', { carrier: currentCarrier }) }}
        </span>
        <button @click="loadCards(true)" :disabled="loading" class="px-3 py-2 rounded-xl bg-slate-200 dark:bg-white/10 text-slate-700 dark:text-white/80 disabled:opacity-50">
          <i class="fas fa-sync-alt mr-1" :class="{ 'animate-spin': loading }"></i>{{ t('common.refresh') }}
        </button>
      </div>
    </div>

    <div v-if="unsupported" class="mt-4 rounded-xl border border-amber-500/30 bg-amber-500/10 p-4 text-sm text-amber-500">
      {{ t('builtinCard.unsupported') }}
    </div>
    <div v-else-if="loading" class="py-10 text-center text-slate-500 dark:text-white/50">{{ t('builtinCard.loading') }}</div>
    <div v-else-if="cards.length" class="mt-4 grid gap-3 md:grid-cols-2">
      <div v-for="card in cards" :key="card.slot" class="rounded-xl bg-slate-100 dark:bg-white/5 border border-slate-200 dark:border-white/10 p-4">
        <div class="flex items-start justify-between gap-3">
          <div>
            <p class="font-bold text-slate-900 dark:text-white">{{ card.operatorName }}</p>
            <p class="text-sm text-slate-500 dark:text-white/50">{{ card.slot }}</p>
          </div>
          <span class="px-2 py-1 rounded-lg text-xs" :class="card.realState === 2 ? 'bg-green-500/10 text-green-500' : 'bg-slate-500/10 text-slate-500'">
            {{ card.realState === 2 ? t('builtinCard.ready') : t('builtinCard.unavailable') }}
          </span>
        </div>
        <p class="mt-3 text-sm text-slate-500 dark:text-white/50">{{ t('builtinCard.iccid') }}：{{ card.iccid }}</p>
        <p v-if="realNameStatus[card.operatorId] !== undefined" class="mt-1 text-xs" :class="realNameStatus[card.operatorId] === 2 ? 'text-green-500' : 'text-amber-500'">
          {{ t('builtinCard.realName') }}：{{ realNameStatus[card.operatorId] === 2 ? t('builtinCard.verifiedShort') : t('builtinCard.notVerifiedShort') }}
        </p>
        <div class="mt-4 flex gap-2">
          <button @click="checkRealName(card)" :disabled="busy !== null" class="flex-1 px-3 py-2 rounded-xl bg-white dark:bg-white/10 text-slate-700 dark:text-white/80 disabled:opacity-50">
            {{ t('builtinCard.checkRealName') }}
          </button>
          <button @click="switchCard(card)" :disabled="busy !== null || card.realState !== 2" class="flex-1 px-3 py-2 rounded-xl bg-blue-500 text-white disabled:opacity-50">
            {{ t('builtinCard.switch') }}
          </button>
        </div>
      </div>
    </div>
    <div v-else class="mt-4 rounded-xl bg-slate-100 dark:bg-white/5 p-4 text-sm text-slate-500 dark:text-white/50">
      {{ t('builtinCard.empty') }}
    </div>
  </section>
</template>
