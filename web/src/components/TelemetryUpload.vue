<script setup>
import { computed, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import {
  getTelemetryConfig,
  getTelemetryStatus,
  saveTelemetryConfig,
  testTelemetry
} from '../composables/useApi'
import { useToast } from '../composables/useToast'

const { t } = useI18n()
const { success, error } = useToast()

const loading = ref(true)
const saving = ref(false)
const testing = ref(false)
const tokenInput = ref('')
const clearToken = ref(false)
const config = ref({
  enabled: false,
  url: '',
  interval_sec: 60,
  token_configured: false
})
const status = ref({
  running: false,
  last_success: 0,
  last_failure: 0,
  sent_count: 0,
  failed_count: 0,
  last_error: ''
})

const httpWarning = computed(() => config.value.url?.startsWith('http://'))

function formatTime(timestamp) {
  if (!timestamp) return t('settings.telemetryNever')
  return new Date(timestamp * 1000).toLocaleString()
}

async function load() {
  loading.value = true
  try {
    const [savedConfig, savedStatus] = await Promise.all([
      getTelemetryConfig(),
      getTelemetryStatus()
    ])
    config.value = {
      ...config.value,
      ...savedConfig,
      enabled: Boolean(savedConfig.enabled)
    }
    status.value = { ...status.value, ...savedStatus }
  } catch (err) {
    error(t('settings.telemetryLoadFailed') + ': ' + err.message)
  } finally {
    loading.value = false
  }
}

async function save() {
  const interval = Number(config.value.interval_sec)
  if (!Number.isInteger(interval) || interval < 10 || interval > 3600) {
    error(t('settings.telemetryIntervalInvalid'))
    return
  }
  if (config.value.enabled && !config.value.url.trim()) {
    error(t('settings.telemetryUrlRequired'))
    return
  }
  saving.value = true
  try {
    const payload = {
      enabled: config.value.enabled ? 1 : 0,
      url: config.value.url.trim(),
      interval_sec: interval
    }
    if (tokenInput.value.trim()) payload.token = tokenInput.value.trim()
    if (clearToken.value) payload.clear_token = true
    await saveTelemetryConfig(payload)
    tokenInput.value = ''
    clearToken.value = false
    success(t('settings.telemetrySaved'))
    await load()
  } catch (err) {
    error(t('settings.telemetrySaveFailed') + ': ' + err.message)
  } finally {
    saving.value = false
  }
}

async function testConnection() {
  testing.value = true
  try {
    await testTelemetry()
    success(t('settings.telemetryTestSuccess'))
  } catch (err) {
    error(t('settings.telemetryTestFailed') + ': ' + err.message)
  } finally {
    testing.value = false
    await load()
  }
}

onMounted(load)
</script>

<template>
  <section class="rounded-2xl bg-white/95 dark:bg-white/5 backdrop-blur border border-slate-200/60 dark:border-white/10 p-6 shadow-lg shadow-slate-200/40 dark:shadow-black/20">
    <div class="flex items-start justify-between gap-4 mb-6">
      <div>
        <h3 class="text-slate-900 dark:text-white font-semibold flex items-center">
          <i class="fas fa-cloud-arrow-up text-cyan-500 mr-2"></i>
          {{ t('settings.telemetryTitle') }}
        </h3>
        <p class="text-slate-500 dark:text-white/50 text-sm mt-1">{{ t('settings.telemetryDesc') }}</p>
      </div>
      <label class="relative cursor-pointer shrink-0">
        <input v-model="config.enabled" type="checkbox" class="sr-only peer" :disabled="loading || saving">
        <div class="w-14 h-7 bg-slate-200 dark:bg-white/10 rounded-full peer-checked:bg-cyan-500 transition-colors"></div>
        <div class="absolute top-0.5 left-0.5 w-6 h-6 bg-white rounded-full shadow transition-transform peer-checked:translate-x-7"></div>
      </label>
    </div>

    <div v-if="loading" class="text-sm text-slate-500 dark:text-white/50">{{ t('common.loading') }}</div>
    <div v-else class="space-y-4">
      <div>
        <label class="block text-slate-600 dark:text-white/60 text-sm mb-2">{{ t('settings.telemetryUrl') }}</label>
        <input v-model="config.url" type="url" class="w-full px-4 py-3 bg-slate-50 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white" placeholder="https://example.com/v1/ingest">
      </div>

      <div>
        <label class="block text-slate-600 dark:text-white/60 text-sm mb-2">{{ t('settings.telemetryToken') }}</label>
        <input v-model="tokenInput" type="password" autocomplete="new-password" class="w-full px-4 py-3 bg-slate-50 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white" :placeholder="config.token_configured ? t('settings.telemetryTokenConfigured') : t('settings.telemetryTokenPlaceholder')">
        <label v-if="config.token_configured" class="flex items-center gap-2 mt-2 text-sm text-slate-500 dark:text-white/50">
          <input v-model="clearToken" type="checkbox">
          {{ t('settings.telemetryClearToken') }}
        </label>
      </div>

      <div>
        <label class="block text-slate-600 dark:text-white/60 text-sm mb-2">{{ t('settings.telemetryInterval') }}</label>
        <input v-model.number="config.interval_sec" type="number" min="10" max="3600" class="w-full px-4 py-3 bg-slate-50 dark:bg-white/10 border border-slate-200 dark:border-white/20 rounded-xl text-slate-900 dark:text-white">
      </div>

      <p v-if="httpWarning" class="text-amber-600 dark:text-amber-400 text-sm bg-amber-500/10 rounded-xl p-3">
        <i class="fas fa-triangle-exclamation mr-2"></i>{{ t('settings.telemetryHttpWarning') }}
      </p>

      <div class="flex flex-wrap gap-3">
        <button @click="save" :disabled="saving" class="px-4 py-2.5 rounded-xl bg-gradient-to-r from-cyan-500 to-blue-500 text-white disabled:opacity-50">
          <i :class="saving ? 'fas fa-spinner fa-spin' : 'fas fa-save'" class="mr-2"></i>{{ saving ? t('settings.saving') : t('settings.telemetrySave') }}
        </button>
        <button @click="testConnection" :disabled="testing || !config.url" class="px-4 py-2.5 rounded-xl border border-slate-200 dark:border-white/20 text-slate-700 dark:text-white/80 disabled:opacity-50">
          <i :class="testing ? 'fas fa-spinner fa-spin' : 'fas fa-plug'" class="mr-2"></i>{{ t('settings.telemetryTest') }}
        </button>
      </div>

      <div class="grid grid-cols-2 md:grid-cols-4 gap-3 text-sm">
        <div class="p-3 rounded-xl bg-slate-50 dark:bg-white/5"><span class="block text-slate-500 dark:text-white/50">{{ t('settings.telemetryRunning') }}</span><span class="font-medium text-slate-900 dark:text-white">{{ status.running ? t('common.enabled') : t('common.disabled') }}</span></div>
        <div class="p-3 rounded-xl bg-slate-50 dark:bg-white/5"><span class="block text-slate-500 dark:text-white/50">{{ t('settings.telemetryLastSuccess') }}</span><span class="font-medium text-slate-900 dark:text-white">{{ formatTime(status.last_success) }}</span></div>
        <div class="p-3 rounded-xl bg-slate-50 dark:bg-white/5"><span class="block text-slate-500 dark:text-white/50">{{ t('settings.telemetrySentCount') }}</span><span class="font-medium text-slate-900 dark:text-white">{{ status.sent_count }}</span></div>
        <div class="p-3 rounded-xl bg-slate-50 dark:bg-white/5"><span class="block text-slate-500 dark:text-white/50">{{ t('settings.telemetryFailedCount') }}</span><span class="font-medium text-slate-900 dark:text-white">{{ status.failed_count }}</span></div>
      </div>
      <p v-if="status.last_error" class="text-sm text-red-500">{{ status.last_error }}</p>
    </div>
  </section>
</template>
