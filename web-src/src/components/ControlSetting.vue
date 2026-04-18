<template>
  <fieldset :disabled="disabled" class="field">
    <label v-if="$slots.label" class="label has-text-weight-normal">
      <slot name="label" />
    </label>
    <div class="control" :class="{ 'has-icons-right': isSuccess || isError }">
      <slot name="input" :label="label" :update="update" />
      <mdicon
        v-if="$slots.label && (isSuccess || isError)"
        class="icon is-right"
        :name="isSuccess ? 'check' : 'close'"
        size="16"
      />
    </div>
    <div v-if="$slots.help" class="help mb-4">
      <slot name="help" />
    </div>
  </fieldset>
</template>

<script setup>
import { computed, ref } from 'vue'
import settings from '@/api/settings'
import { useI18n } from 'vue-i18n'
import { useSettingsStore } from '@/stores/settings'

const { t } = useI18n()
const settingsStore = useSettingsStore()

const props = defineProps({
  disabled: Boolean,
  placeholder: { default: '', type: String },
  setting: { required: true, type: Object }
})

const timerDelay = 2000
const timerId = ref(-1)

const isError = computed(() => timerId.value === -2)
const isSuccess = computed(() => timerId.value >= 0)

const label = computed(() =>
  t(
    `settings.${props.setting.category}.${props.setting.name.replace(/_/gu, '-')}`
  )
)

const update = async (event, sanitise) => {
  const value = sanitise?.(event.target)
  if (value === props.setting.value) {
    return
  }
  const setting = { ...props.setting, value }
  try {
    await settings.update(setting)
    window.clearTimeout(timerId.value)
    settingsStore.update(setting)
  } catch {
    timerId.value = -2
  } finally {
    timerId.value = window.setTimeout(() => {
      timerId.value = -1
    }, timerDelay)
  }
}
</script>
