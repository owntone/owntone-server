<template>
  <control-setting
    :category="category"
    :disabled="disabled"
    :name="name"
    :placeholder="placeholder"
  >
    <template #input="{ label, setting, update }">
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

<script>
import ControlSetting from '@/components/ControlSetting.vue'

export default {
  name: 'ControlSettingIntegerField',
  components: { ControlSetting },
  props: {
    category: { required: true, type: String },
    disabled: Boolean,
    name: { required: true, type: String },
    placeholder: { default: '', type: String }
  },
  methods: {
    sanitise(target) {
      const value = parseInt(target.value.replace(/\D+/gu, ''), 10) || 0
      return (target.value = value)
    }
  }
}
</script>
