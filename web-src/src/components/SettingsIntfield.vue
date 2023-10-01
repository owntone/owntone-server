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
          type="number"
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
import * as types from '@/store/mutation_types'
import webapi from '@/webapi'

export default {
  name: 'SettingsIntfield',
  props: ['category_name', 'option_name', 'placeholder', 'disabled'],

  data() {
    return {
      timerDelay: 2000,
      timerId: -1,
      statusUpdate: ''
    }
  },

  computed: {
    category() {
      return this.$store.state.settings.categories.find(
        (elem) => elem.name === this.category_name
      )
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
    },

    info() {
      if (this.statusUpdate === 'success') {
        return this.$t('setting.saved')
      } else if (this.statusUpdate === 'error') {
        return this.$t('setting.not-saved')
      }
      return ''
    },

    is_success() {
      return this.statusUpdate === 'success'
    },

    is_error() {
      return this.statusUpdate === 'error'
    }
  },

  methods: {
    set_update_timer() {
      if (this.timerId > 0) {
        window.clearTimeout(this.timerId)
        this.timerId = -1
      }

      this.statusUpdate = ''
      const newValue = this.$refs.setting.value
      if (newValue !== this.value) {
        this.timerId = window.setTimeout(this.update_setting, this.timerDelay)
      }
    },

    update_setting() {
      this.timerId = -1

      const newValue = this.$refs.setting.value
      if (newValue === this.value) {
        this.statusUpdate = ''
        return
      }

      const option = {
        category: this.category.name,
        name: this.option_name,
        value: parseInt(newValue, 10)
      }
      webapi
        .settings_update(this.category.name, option)
        .then(() => {
          this.$store.commit(types.UPDATE_SETTINGS_OPTION, option)
          this.statusUpdate = 'success'
        })
        .catch(() => {
          this.statusUpdate = 'error'
          this.$refs.setting.value = this.value
        })
        .finally(() => {
          this.timerId = window.setTimeout(this.clear_status, this.timerDelay)
        })
    },

    clear_status() {
      this.statusUpdate = ''
    }
  }
}
</script>

<style></style>
