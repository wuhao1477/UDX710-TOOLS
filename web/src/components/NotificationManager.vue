<script setup>
import { computed, onMounted, ref } from 'vue'
import { useI18n } from 'vue-i18n'
import { useConfirm } from '../composables/useConfirm'
import {
  addNotificationRule,
  deleteNotificationRule,
  getNotificationLogs,
  getNotificationRules,
  getNotificationWebhook,
  saveNotificationWebhook,
  testNotificationWebhook,
  updateNotificationRule
} from '../composables/useApi'
import { useToast } from '../composables/useToast'

const { t } = useI18n()
const { confirm } = useConfirm()
const { success, error } = useToast()
const activeSection = ref('webhook')
const showTutorial = ref(false)
const showLogs = ref(false)
const showRuleDialog = ref(false)
const editingRuleId = ref(null)
const loading = ref(false)
const saving = ref(false)
const webhookConfig = ref({
  enabled: false,
  platform: 'pushplus',
  url: 'http://www.pushplus.plus/send',
  body: '{"token":"YOUR_TOKEN","title":"新短信","content":"发件人: #{sender}\\n内容: #{content}"}',
  headers: 'Content-Type: application/json'
})
const rules = ref([])
const logs = ref([])
const ruleForm = ref({
  event_type: '',
  threshold: 20,
  threshold_unit: 'percent',
  cooldown_sec: 300
})

const platformTemplates = {
  pushplus: { url: 'http://www.pushplus.plus/send', body: '{"token":"YOUR_TOKEN","title":"新短信","content":"发件人: #{sender}\\n内容: #{content}"}', headers: 'Content-Type: application/json' },
  serverchan: { url: 'https://sctapi.ftqq.com/YOUR_KEY.send', body: '{"title":"新短信","desp":"发件人: #{sender}\\n内容: #{content}"}', headers: 'Content-Type: application/json' },
  bark: { url: 'https://api.day.app/YOUR_KEY/新短信/#{content}', body: '', headers: '' },
  dingtalk: { url: 'https://oapi.dingtalk.com/robot/send?access_token=YOUR_TOKEN', body: '{"msgtype":"text","text":{"content":"新短信\\n发件人: #{sender}\\n内容: #{content}"}}', headers: 'Content-Type: application/json' },
  feishu: { url: 'https://open.feishu.cn/open-apis/bot/v2/hook/YOUR_TOKEN', body: '{"msg_type":"text","content":{"text":"新短信\\n发件人: #{sender}\\n内容: #{content}"}}', headers: 'Content-Type: application/json' },
  discord: { url: 'https://discord.com/api/webhooks/YOUR_WEBHOOK', body: '{"content":"**新短信**\\n发件人: #{sender}\\n内容: #{content}"}', headers: 'Content-Type: application/json' },
  custom: { url: '', body: '', headers: '' }
}

const eventMeta = [
  ['sms_received', 'smsReceived', false], ['login', 'login', false],
  ['rj45_changed', 'rj45Changed', false],
  ['network_interface_changed', 'networkInterfaceChanged', false],
  ['ip_changed', 'ipChanged', false], ['device_changed', 'deviceChanged', false],
  ['operator_changed', 'operatorChanged', false], ['signal_low', 'signalLow', true],
  ['traffic_threshold', 'trafficThreshold', true],
  ['network_type_changed', 'networkTypeChanged', false],
  ['device_status', 'deviceStatus', false]
].map(([id, key, threshold]) => ({ id, label: `notification.events.${key}`, threshold }))

const availableEvents = computed(() => eventMeta.filter(meta =>
  !rules.value.some(rule => rule.event_type === meta.id) ||
  meta.id === ruleForm.value.event_type
))

function eventLabel(id) {
  const meta = eventMeta.find(item => item.id === id)
  return meta ? t(meta.label) : id
}

function eventMetaFor(id) {
  return eventMeta.find(item => item.id === id)
}

function ruleNeedsThreshold(id) {
  return eventMetaFor(id)?.threshold === true
}

function defaultUnit(id) {
  return id === 'signal_low' || id === 'traffic_threshold' ? 'percent' : 'state'
}

function resetRuleForm() {
  const first = availableEvents.value[0]
  ruleForm.value = {
    event_type: first?.id || '',
    threshold: first?.id === 'signal_low' ? 20 : first?.id === 'traffic_threshold' ? 80 : 0,
    threshold_unit: defaultUnit(first?.id),
    cooldown_sec: 300
  }
}

function openAddRule() {
  if (!availableEvents.value.length) {
    error(t('notification.allEventsAdded'))
    return
  }
  editingRuleId.value = null
  resetRuleForm()
  showRuleDialog.value = true
}

function openEditRule(rule) {
  editingRuleId.value = rule.id
  ruleForm.value = {
    event_type: rule.event_type,
    threshold: rule.threshold,
    threshold_unit: rule.threshold_unit,
    cooldown_sec: rule.cooldown_sec
  }
  showRuleDialog.value = true
}

function closeRuleDialog() {
  showRuleDialog.value = false
  editingRuleId.value = null
}

async function saveRule() {
  if (!ruleForm.value.event_type) return
  saving.value = true
  const isEditing = Boolean(editingRuleId.value)
  try {
    const payload = { ...ruleForm.value, enabled: true }
    if (editingRuleId.value) await updateNotificationRule(editingRuleId.value, payload)
    else await addNotificationRule(payload)
    await loadRules()
    closeRuleDialog()
    success(isEditing ? t('notification.ruleUpdated') : t('notification.ruleAdded'))
  } catch (err) {
    error(err.message || t('notification.saveFailed'))
  } finally {
    saving.value = false
  }
}

async function removeRule(rule) {
  if (!await confirm({ title: t('notification.deleteRule'), message: t('notification.confirmDeleteRule', { event: eventLabel(rule.event_type) }), danger: true })) return
  try {
    await deleteNotificationRule(rule.id)
    await loadRules()
    success(t('notification.ruleDeleted'))
  } catch (err) {
    error(err.message || t('notification.deleteFailed'))
  }
}

async function loadRules() {
  const data = await getNotificationRules()
  rules.value = data.rules || []
}

async function loadConfig() {
  loading.value = true
  try {
    const [webhook] = await Promise.all([getNotificationWebhook(), loadRules()])
    webhookConfig.value = { ...webhookConfig.value, ...webhook }
  } catch (err) {
    error(err.message || t('notification.loadFailed'))
  } finally {
    loading.value = false
  }
}

function applyTemplate(platform) {
  const template = platformTemplates[platform]
  if (template) webhookConfig.value = { ...webhookConfig.value, ...template }
}

async function saveWebhook() {
  saving.value = true
  try {
    await saveNotificationWebhook(webhookConfig.value)
    success(t('notification.webhookSaved'))
  } catch (err) {
    error(err.message || t('notification.saveFailed'))
  } finally {
    saving.value = false
  }
}

async function testWebhook() {
  try {
    await testNotificationWebhook()
    success(t('notification.testQueued'))
  } catch (err) {
    error(err.message || t('notification.testFailed'))
  }
}

async function loadLogs() {
  try {
    const data = await getNotificationLogs(30)
    logs.value = data.data || []
  } catch (err) {
    error(err.message || t('notification.logsFailed'))
  }
}

function openLogs() {
  loadLogs()
  showLogs.value = true
}

function formatLogTime(timestamp) {
  return timestamp ? new Date(timestamp * 1000).toLocaleString() : ''
}

onMounted(loadConfig)
</script>

<template>
  <div class="space-y-6">
    <div class="flex items-center justify-between gap-4">
      <div class="flex items-center gap-3"><div class="w-11 h-11 rounded-2xl bg-gradient-to-br from-orange-500 to-amber-500 flex items-center justify-center"><i class="fas fa-bell text-white"></i></div><div><h2 class="text-xl font-bold text-slate-900 dark:text-white">{{ t('notification.title') }}</h2><p class="text-sm text-slate-500 dark:text-white/50">{{ t('notification.subtitle') }}</p></div></div>
      <button @click="loadConfig" :disabled="loading" class="px-3 py-2 rounded-xl bg-slate-100 dark:bg-white/10 text-slate-600 dark:text-white/70"><i class="fas fa-sync-alt mr-1" :class="{ 'animate-spin': loading }"></i>{{ t('common.refresh') }}</button>
    </div>

    <div class="flex gap-2 rounded-2xl bg-slate-100 dark:bg-white/5 p-2 border border-slate-200 dark:border-white/10"><button @click="activeSection = 'webhook'" class="flex-1 px-4 py-3 rounded-xl text-sm font-medium" :class="activeSection === 'webhook' ? 'bg-white dark:bg-white/10 text-orange-600 dark:text-orange-400 shadow-sm' : 'text-slate-500 dark:text-white/50'">{{ t('notification.webhook') }}</button><button @click="activeSection = 'events'" class="flex-1 px-4 py-3 rounded-xl text-sm font-medium" :class="activeSection === 'events' ? 'bg-white dark:bg-white/10 text-orange-600 dark:text-orange-400 shadow-sm' : 'text-slate-500 dark:text-white/50'">{{ t('notification.eventsTitle') }}</button></div>

    <section v-if="activeSection === 'webhook'" class="rounded-3xl bg-white dark:bg-white/5 border border-slate-200 dark:border-white/10 p-5 sm:p-6 space-y-6">
      <div class="flex items-center justify-between gap-3"><div><h3 class="font-bold text-slate-900 dark:text-white">{{ t('sms.webhookConfig') }}</h3><p class="text-xs text-slate-500 dark:text-white/40 mt-1">{{ t('notification.webhookDesc') }}</p></div><div class="flex gap-2"><button @click="showTutorial = true" class="px-3 py-2 rounded-xl bg-violet-500/10 text-violet-600 dark:text-violet-400" :title="t('sms.viewTutorial')"><i class="fas fa-book"></i></button><button @click="openLogs" class="px-3 py-2 rounded-xl bg-amber-500/10 text-amber-600 dark:text-amber-400" :title="t('sms.viewSendLogs')"><i class="fas fa-history"></i></button></div></div>
      <div class="grid gap-5 md:grid-cols-2"><div class="md:col-span-2 flex items-center justify-between p-4 rounded-2xl bg-slate-50 dark:bg-white/5 border border-slate-200 dark:border-white/10"><div><p class="font-medium text-slate-900 dark:text-white">{{ t('sms.enableWebhook') }}</p><p class="text-xs text-slate-500 dark:text-white/40 mt-1">{{ t('notification.enableDesc') }}</p></div><label class="relative inline-flex items-center cursor-pointer"><input v-model="webhookConfig.enabled" type="checkbox" class="sr-only peer"><div class="w-14 h-7 bg-slate-200 dark:bg-white/10 rounded-full peer-checked:bg-orange-500 after:content-[''] after:absolute after:top-0.5 after:left-1 after:bg-white after:rounded-full after:h-6 after:w-6 after:transition-all peer-checked:after:translate-x-7"></div></label></div><div><label class="field-label">{{ t('sms.selectPlatform') }}</label><select v-model="webhookConfig.platform" @change="applyTemplate(webhookConfig.platform)" class="field-input"><option value="pushplus">PushPlus</option><option value="serverchan">Server酱</option><option value="bark">Bark</option><option value="dingtalk">钉钉机器人</option><option value="feishu">飞书机器人</option><option value="discord">Discord</option><option value="custom">{{ t('sms.customPlatform') }}</option></select></div><div><label class="field-label">{{ t('sms.webhookUrl') }}</label><input v-model="webhookConfig.url" class="field-input font-mono text-sm" placeholder="https://api.example.com/webhook"></div><div class="md:col-span-2"><label class="field-label">{{ t('sms.requestBody') }}</label><textarea v-model="webhookConfig.body" rows="5" class="field-input font-mono text-sm"></textarea><p class="field-help">{{ t('notification.templateHelp') }}</p></div><div class="md:col-span-2"><label class="field-label">{{ t('sms.requestHeaders') }}</label><textarea v-model="webhookConfig.headers" rows="2" class="field-input font-mono text-sm"></textarea></div></div>
      <div class="flex justify-end gap-3"><button @click="testWebhook" :disabled="saving" class="action-secondary"><i class="fas fa-paper-plane mr-1"></i>{{ t('sms.test') }}</button><button @click="saveWebhook" :disabled="saving" class="action-primary"><i class="fas fa-save mr-1"></i>{{ t('sms.save') }}</button></div>
    </section>

    <section v-else class="space-y-4"><div class="flex items-center justify-between gap-3"><div><h3 class="font-bold text-slate-900 dark:text-white">{{ t('notification.eventsTitle') }}</h3><p class="text-xs text-slate-500 dark:text-white/40 mt-1">{{ t('notification.eventsDesc') }}</p></div><button @click="openAddRule" class="action-primary"><i class="fas fa-plus mr-1"></i>{{ t('notification.addRule') }}</button></div><div v-if="!rules.length" class="rounded-2xl bg-white dark:bg-white/5 border border-dashed border-slate-300 dark:border-white/20 p-12 text-center text-slate-500 dark:text-white/50"><i class="fas fa-bell-slash text-3xl mb-3"></i><p>{{ t('notification.emptyRules') }}</p><button @click="openAddRule" class="mt-4 action-primary">{{ t('notification.addFirstRule') }}</button></div><div v-else class="grid gap-3 lg:grid-cols-2"><div v-for="rule in rules" :key="rule.id" class="rounded-2xl bg-white dark:bg-white/5 border border-slate-200 dark:border-white/10 p-4"><div class="flex items-start justify-between gap-3"><div><p class="font-medium text-slate-900 dark:text-white">{{ eventLabel(rule.event_type) }}</p><p class="text-xs text-slate-500 dark:text-white/40 mt-1">{{ t(`${eventMetaFor(rule.event_type)?.label}Desc`) }}</p></div><div class="flex gap-1"><button @click="openEditRule(rule)" class="w-8 h-8 rounded-lg bg-slate-100 dark:bg-white/10 text-slate-500"><i class="fas fa-pen"></i></button><button @click="removeRule(rule)" class="w-8 h-8 rounded-lg bg-red-500/10 text-red-500"><i class="fas fa-trash"></i></button></div></div><div class="flex flex-wrap gap-2 mt-4 text-xs"><span v-if="rule.threshold_unit !== 'state'" class="px-2 py-1 rounded-lg bg-orange-500/10 text-orange-600 dark:text-orange-400">{{ t('notification.threshold') }} {{ rule.threshold }} {{ rule.threshold_unit }}</span><span class="px-2 py-1 rounded-lg bg-slate-100 dark:bg-white/10 text-slate-500 dark:text-white/50">{{ t('notification.cooldown') }} {{ rule.cooldown_sec }}s</span><span class="px-2 py-1 rounded-lg bg-emerald-500/10 text-emerald-600 dark:text-emerald-400">{{ t('notification.active') }}</span></div></div></div></section>

    <Teleport to="body"><Transition name="fade"><div v-if="showRuleDialog" class="fixed inset-0 z-50 flex items-center justify-center p-4"><div class="absolute inset-0 bg-black/60" @click="closeRuleDialog"></div><div class="relative w-full max-w-lg rounded-3xl bg-white dark:bg-slate-800 p-6 shadow-2xl"><div class="flex items-center justify-between mb-5"><h3 class="font-bold text-slate-900 dark:text-white">{{ editingRuleId ? t('notification.editRule') : t('notification.addRule') }}</h3><button @click="closeRuleDialog" class="w-9 h-9 rounded-xl bg-slate-100 dark:bg-white/10"><i class="fas fa-times"></i></button></div><div class="space-y-4"><div><label class="field-label">{{ t('notification.selectEvent') }}</label><select v-model="ruleForm.event_type" :disabled="!!editingRuleId" class="field-input"><option v-for="meta in availableEvents" :key="meta.id" :value="meta.id">{{ t(meta.label) }}</option></select></div><div v-if="ruleNeedsThreshold(ruleForm.event_type)" class="grid grid-cols-2 gap-3"><div><label class="field-label">{{ t('notification.threshold') }}</label><input v-model.number="ruleForm.threshold" type="number" :min="ruleForm.threshold_unit === 'dbm' ? -150 : 0" class="field-input"></div><div><label class="field-label">{{ t('notification.unit') }}</label><select v-model="ruleForm.threshold_unit" class="field-input"><option v-if="ruleForm.event_type === 'signal_low'" value="percent">{{ t('notification.percent') }}</option><option v-if="ruleForm.event_type === 'signal_low'" value="dbm">dBm</option><option v-if="ruleForm.event_type === 'traffic_threshold'" value="percent">{{ t('notification.percent') }}</option><option v-if="ruleForm.event_type === 'traffic_threshold'" value="bytes">Bytes</option></select></div></div><div><label class="field-label">{{ t('notification.cooldown') }} (s)</label><input v-model.number="ruleForm.cooldown_sec" type="number" min="0" max="86400" class="field-input"></div></div><div class="flex justify-end gap-3 mt-6"><button @click="closeRuleDialog" class="action-secondary">{{ t('common.cancel') }}</button><button @click="saveRule" :disabled="saving" class="action-primary">{{ t('common.save') }}</button></div></div></div></Transition></Teleport>

    <Teleport to="body"><Transition name="fade"><div v-if="showTutorial" class="fixed inset-0 z-50 flex items-center justify-center p-4"><div class="absolute inset-0 bg-black/60" @click="showTutorial = false"></div><div class="relative w-full max-w-2xl max-h-[85vh] overflow-y-auto rounded-3xl bg-white dark:bg-slate-800 p-6 shadow-2xl"><div class="flex items-center justify-between mb-5"><h3 class="font-bold text-slate-900 dark:text-white">{{ t('sms.tutorialTitle') }}</h3><button @click="showTutorial = false" class="w-9 h-9 rounded-xl bg-slate-100 dark:bg-white/10"><i class="fas fa-times"></i></button></div><p class="text-sm text-slate-600 dark:text-white/70 mb-5">{{ t('sms.tutorialIntroDesc') }}</p><div class="grid grid-cols-2 sm:grid-cols-3 gap-2 mb-5"><div v-for="platform in ['PushPlus','Server酱','Bark','钉钉','飞书','Discord']" :key="platform" class="p-3 rounded-xl bg-slate-50 dark:bg-white/5 text-center text-sm text-slate-700 dark:text-white/80">{{ platform }}</div></div><div class="p-4 rounded-xl bg-slate-900 text-sm font-mono text-cyan-300 break-all">#{event} #{title} #{message} #{sender} #{content} #{time}</div></div></div></Transition></Teleport>

    <Teleport to="body"><Transition name="fade"><div v-if="showLogs" class="fixed inset-0 z-50 flex items-center justify-center p-4"><div class="absolute inset-0 bg-black/60" @click="showLogs = false"></div><div class="relative w-full max-w-2xl max-h-[85vh] overflow-y-auto rounded-3xl bg-white dark:bg-slate-800 p-6 shadow-2xl"><div class="flex items-center justify-between mb-5"><h3 class="font-bold text-slate-900 dark:text-white">{{ t('notification.logsTitle') }}</h3><div class="flex gap-2"><button @click="loadLogs" class="w-9 h-9 rounded-xl bg-slate-100 dark:bg-white/10"><i class="fas fa-sync-alt"></i></button><button @click="showLogs = false" class="w-9 h-9 rounded-xl bg-slate-100 dark:bg-white/10"><i class="fas fa-times"></i></button></div></div><div v-if="!logs.length" class="py-10 text-center text-slate-500 dark:text-white/50">{{ t('notification.noLogs') }}</div><div v-for="log in logs" :key="log.id" class="p-3 mb-3 rounded-xl bg-slate-50 dark:bg-white/5 border border-slate-200 dark:border-white/10"><div class="flex items-center justify-between gap-2"><span class="font-medium text-slate-900 dark:text-white">{{ log.title || eventLabel(log.event) }}</span><span :class="log.result ? 'text-emerald-500' : 'text-red-500'" class="text-xs">{{ log.result ? t('notification.sent') : t('notification.failed') }}</span></div><p class="text-xs text-slate-400 mt-1">{{ formatLogTime(log.created_at) }}</p><details class="mt-2"><summary class="text-xs text-slate-500 cursor-pointer">{{ t('sms.requestContent') }}</summary><pre class="mt-2 text-xs whitespace-pre-wrap break-all">{{ log.request }}</pre></details></div></div></div></Transition></Teleport>
  </div>
</template>

<style scoped>
.field-label { display: block; margin-bottom: .5rem; font-size: .875rem; color: rgb(100 116 139); }
.field-input { width: 100%; padding: .75rem 1rem; border-radius: .75rem; border: 1px solid rgb(226 232 240); background: rgb(248 250 252); color: rgb(15 23 42); outline: none; }
.field-help { margin-top: .35rem; font-size: .75rem; color: rgb(148 163 184); }
.action-primary, .action-secondary { padding: .65rem 1rem; border-radius: .75rem; font-size: .875rem; }
.action-primary { color: rgb(194 65 12); background: rgb(255 237 213); }
.action-secondary { color: rgb(71 85 105); background: rgb(241 245 249); }
:global(.dark) .field-input { border-color: rgb(255 255 255 / .1); background: rgb(255 255 255 / .05); color: white; }
.fade-enter-active, .fade-leave-active { transition: all .2s ease; }
.fade-enter-from, .fade-leave-to { opacity: 0; }
</style>
