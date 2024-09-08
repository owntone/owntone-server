<template>
  <fieldset :disabled="disabled">
    <div class="field">
      <label class="label has-text-weight-normal">
        <slot name="label" />
        <i
          class="is-size-7"
          :class="{ 'has-text-info': is_success, 'has-text-danger': is_error }"
          v-text="info"
        />
      </label>
      <div class="control">
        <input
          ref="setting"
          class="column input is-one-fifth"
          inputmode="numeric"
          min="0"
          :placeholder="placeholder"
          :value="setting.value"
          @input="set_update_timer"
        />
      </div>
      <p v-if="$slots['info']" class="help">
        <slot name="info" />
      </p>
    </div>
  </fieldset>
</template>

<script>
import { useSettingsStore } from '@/stores/settings'
import webapi from '@/webapi'

export default {
  name: 'SettingsIntfield',
  props: {
    category: { required: true, type: String },
    disabled: Boolean,
    name: { required: true, type: String },
    placeholder: { default: '', type: String }
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
      if (this.statusUpdate === 'success') {
        return this.$t('setting.saved')
      } else if (this.statusUpdate === 'error') {
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
      return this.settingsStore.setting(this.category, this.name)
    }
  },

  methods: {
    clear_status() {
      this.statusUpdate = ''
    },
    set_update_timer(event) {
      event.target.value = event.target.value.replace(/[^0-9]/gu, '')
      if (this.timerId > 0) {
        window.clearTimeout(this.timerId)
        this.timerId = -1
      }
      this.statusUpdate = ''
      this.timerId = window.setTimeout(this.update_setting, this.timerDelay)
    },
    update_setting() {
      this.timerId = -1
      const newValue = parseInt(this.$refs.setting.value, 10)
      if (isNaN(newValue) || newValue === this.value) {
        this.statusUpdate = ''
        return
      }
      const setting = {
        category: this.category,
        name: this.name,
        value: newValue
      }
      webapi
        .settings_update(this.category, setting)
        .then(() => {
          this.settingsStore.update(setting)
          this.statusUpdate = 'success'
        })
        .catch(() => {
          this.statusUpdate = 'error'
          this.$refs.setting.value = this.value
        })
        .finally(() => {
          this.timerId = window.setTimeout(this.clear_status, this.timerDelay)
        })
    }
  }
}
</script>
