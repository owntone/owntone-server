<template>
  <control-switch v-model="setting.value" @update:model-value="update">
    <template #label>
      <slot name="label" />
    </template>
    <template #info>
      <mdicon
        v-if="isSuccess"
        class="icon has-text-info"
        name="check"
        size="16"
      />
      <mdicon
        v-if="isError"
        class="icon has-text-danger"
        name="close"
        size="16"
      />
    </template>
  </control-switch>
</template>

<script>
import ControlSwitch from '@/components/ControlSwitch.vue'
import { useSettingsStore } from '@/stores/settings'
import webapi from '@/webapi'

export default {
  name: 'SettingsCheckbox',
  components: { ControlSwitch },
  props: {
    category: { required: true, type: String },
    name: { required: true, type: String }
  },

  setup() {
    return {
      settingsStore: useSettingsStore()
    }
  },

  data() {
    return {
      statusUpdate: '',
      timerDelay: 2000,
      timerId: -1
    }
  },

  computed: {
    isError() {
      return this.statusUpdate === 'error'
    },
    isSuccess() {
      return this.statusUpdate === 'success'
    },
    setting() {
      const setting = this.settingsStore.setting(this.category, this.name)
      if (!setting) {
        return {
          category: this.category,
          name: this.name,
          value: false
        }
      }
      return setting
    }
  },

  methods: {
    clearStatus() {
      if (this.is_error) {
        this.setting.value = !this.setting.value
      }
      this.statusUpdate = ''
    },
    update() {
      this.timerId = -1
      const setting = {
        category: this.category,
        name: this.name,
        value: this.setting.value
      }
      webapi
        .settings_update(this.category, setting)
        .then(() => {
          this.settingsStore.update(setting)
          this.statusUpdate = 'success'
        })
        .catch(() => {
          this.statusUpdate = 'error'
        })
        .finally(() => {
          this.timerId = window.setTimeout(this.clearStatus, this.timerDelay)
        })
    }
  }
}
</script>
