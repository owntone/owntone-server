<template>
  <div class="field">
    <control-switch
      :id="setting.name"
      v-model="setting.value"
      @update:model-value="update_setting"
    >
      <template #label>
        <slot name="label" />
      </template>
    </control-switch>
    <i
      class="is-size-7"
      :class="{ 'has-text-info': is_success, 'has-text-danger': is_error }"
      v-text="info"
    />
    <p v-if="$slots['info']" class="help">
      <slot name="info" />
    </p>
  </div>
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
    info() {
      if (this.is_success) {
        return this.$t('setting.saved')
      } else if (this.is_error) {
        return this.$t('setting.not-saved')
      }
      return ''
    },
    is_error() {
      return this.statusUpdate === 'error'
    },
    is_success() {
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
    clear_status() {
      if (this.is_error) {
        this.setting.value = !this.setting.value
      }
      this.statusUpdate = ''
    },
    update_setting() {
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
          this.timerId = window.setTimeout(this.clear_status, this.timerDelay)
        })
    }
  }
}
</script>
