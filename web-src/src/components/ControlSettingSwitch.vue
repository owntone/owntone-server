<template>
  <control-setting :disabled="disabled" :setting="setting">
    <template #input="{ label, update }">
      <control-switch
        :model-value="setting.value"
        @update:model-value="
          (value) => update({ target: { checked: value } }, sanitise)
        "
      >
        <template #label>
          <span v-text="label" />
        </template>
      </control-switch>
    </template>
    <template v-if="$slots.help" #help>
      <slot name="help" />
    </template>
  </control-setting>
</template>

<script setup>
import ControlSetting from '@/components/ControlSetting.vue'
import ControlSwitch from '@/components/ControlSwitch.vue'

defineProps({
  disabled: { default: false, type: Boolean },
  setting: { required: true, type: Object }
})

const sanitise = (target) => target.checked
</script>
