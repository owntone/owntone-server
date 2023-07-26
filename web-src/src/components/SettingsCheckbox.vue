<template>
  <div class="field">
    <input
      :id="option.name"
      v-model="option.value"
      type="checkbox"
      class="switch is-rounded mr-2"
      @change="update_setting"
    />
    <label class="pt-0" :for="option.name">
      <slot name="label" />
    </label>
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
import * as types from '@/store/mutation_types'
import webapi from '@/webapi'

export default {
  name: 'SettingsCheckbox',
  props: ['category_name', 'option_name'],

  data() {
    return {
      timerDelay: 2000,
      timerId: -1,
      statusUpdate: ''
    }
  },

  computed: {
    option() {
      const option = this.$store.getters.settings_option(
        this.category_name,
        this.option_name
      )
      if (!option) {
        return {
          category: this.category_name,
          name: this.option_name,
          value: false
        }
      }
      return option
    },

    info() {
      if (this.is_success) {
        return this.$t('setting.saved')
      } else if (this.is_error) {
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
    update_setting() {
      this.timerId = -1
      const option = {
        category: this.category_name,
        name: this.option_name,
        value: this.option.value
      }
      webapi
        .settings_update(this.category_name, option)
        .then(() => {
          this.$store.commit(types.UPDATE_SETTINGS_OPTION, option)
          this.statusUpdate = 'success'
        })
        .catch(() => {
          this.statusUpdate = 'error'
        })
        .finally(() => {
          this.timerId = window.setTimeout(this.clear_status, this.timerDelay)
        })
    },

    clear_status() {
      if (this.is_error) {
        this.option.value = !this.option.value
      }
      this.statusUpdate = ''
    }
  }
}
</script>

<style></style>
