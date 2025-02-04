<template>
  <control-setting
    :category="category"
    :disabled="disabled"
    :name="name"
    :placeholder="placeholder"
  >
    <template #label>
      <slot name="label" />
    </template>
    <template #input="{ setting, update }">
      <input
        class="input"
        inputmode="text"
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
  name: 'ControlSettingTextField',
  components: { ControlSetting },
  props: {
    category: { required: true, type: String },
    disabled: Boolean,
    name: { required: true, type: String },
    placeholder: { default: '', type: String }
  },
  methods: {
    sanitise(target) {
      return target.value
    }
  }
}
</script>
