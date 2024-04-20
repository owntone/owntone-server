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
          :value="value"
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
import webapi from '@/webapi'

export default {
  name: 'SettingsIntfield',
  props: {
    category_name: { required: true, type: String },
    disabled: Boolean,
    option_name: { required: true, type: String },
    placeholder: { default: '', type: String }
  },

  data() {
    return {
      statusUpdate: '',
      timerDelay: 2000,
      timerId: -1
    }
  },

  computed: {
    category() {
      return this.$store.state.settings.categories.find(
        (elem) => elem.name === this.category_name
      )
    },
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
    option() {
      if (!this.category) {
        return {}
      }
      return this.category.options.find(
        (elem) => elem.name === this.option_name
      )
    },
    value() {
      return this.option.value
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
      const option = {
        category: this.category.name,
        name: this.option_name,
        value: newValue
      }
      webapi
        .settings_update(this.category.name, option)
        .then(() => {
          this.$store.dispatch('update_settings_option', option)
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

<style></style>
