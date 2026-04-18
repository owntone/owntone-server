<template>
  <control-setting
    :disabled="disabled"
    :placeholder="placeholder"
    :setting="setting"
  >
    <template #input="{ label, update }">
      <span v-text="label" />
      <input
        class="input"
        inputmode="numeric"
        min="0"
        :placeholder="placeholder"
        :value="setting.value"
        @input="update($event, sanitise)"
      />
    </template>
    <template #help>
      <slot name="help" />
    </template>
  </control-setting>
</template>

<script setup>
import ControlSetting from '@/components/ControlSetting.vue'

defineProps({
  disabled: Boolean,
  placeholder: { default: '', type: String },
  setting: { required: true, type: Object }
})

const sanitise = (target) => {
  const value = parseInt(target.value.replace(/\D+/gu, ''), 10) || 0
  return (target.value = value)
}
</script>
