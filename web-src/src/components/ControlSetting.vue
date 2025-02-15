<template>
  <fieldset :disabled="disabled">
    <div class="field">
      <label v-if="$slots.label" class="label has-text-weight-normal">
        <slot name="label" />
      </label>
      <div class="control" :class="{ 'has-icons-right': isSuccess || isError }">
        <slot name="input" :setting="setting" :update="update" />
        <mdicon
          v-if="$slots.label && (isSuccess || isError)"
          class="icon is-right"
          :name="isSuccess ? 'check' : 'close'"
          size="16"
        />
      </div>
      <p v-if="$slots.help" class="help mb-4">
        <slot name="help" />
      </p>
    </div>
  </fieldset>
</template>

<script>
import { useSettingsStore } from '@/stores/settings'
import webapi from '@/webapi'

export default {
  name: 'ControlSetting',
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
      timerDelay: 2000,
      timerId: -1
    }
  },

  computed: {
    isError() {
      return this.timerId === -2
    },
    isSuccess() {
      return this.timerId >= 0
    },
    setting() {
      return this.settingsStore.setting(this.category, this.name)
    }
  },

  methods: {
    update(event, sanitise) {
      const value = sanitise?.(event.target)
      if (value === this.setting.value) {
        return
      }
      const setting = {
        category: this.category,
        name: this.name,
        value
      }
      webapi
        .settings_update(this.category, setting)
        .then(() => {
          window.clearTimeout(this.timerId)
          this.settingsStore.update(setting)
        })
        .catch(() => {
          this.timerId = -2
        })
        .finally(() => {
          this.timerId = window.setTimeout(() => {
            this.timerId = -1
          }, this.timerDelay)
        })
    }
  }
}
</script>
