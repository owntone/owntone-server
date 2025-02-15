<template>
  <control-setting :category="category" :disabled="disabled" :name="name">
    <template #input="{ setting, update }">
      <control-switch
        :model-value="setting.value"
        @update:model-value="
          (value) => update({ target: { checked: value } }, sanitise)
        "
      >
        <template #label>
          <slot name="label" />
        </template>
      </control-switch>
    </template>
    <template v-if="$slots.help" #help>
      <slot name="help" />
    </template>
  </control-setting>
</template>

<script>
import ControlSetting from '@/components/ControlSetting.vue'
import ControlSwitch from '@/components/ControlSwitch.vue'

export default {
  name: 'ControlSettingSwitch',
  components: { ControlSetting, ControlSwitch },
  props: {
    category: { required: true, type: String },
    disabled: { default: false, type: Boolean },
    name: { required: true, type: String }
  },
  methods: {
    sanitise(target) {
      return target.checked
    }
  }
}
</script>
