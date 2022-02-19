<template>
  <div class="field">
    <label class="checkbox">
      <input type="checkbox"
          :checked="value"
          @change="set_update_timer"
          ref="settings_checkbox">
      <slot name="label"></slot>
      <i class="is-size-7"
          :class="{
            'has-text-info': statusUpdate === 'success',
            'has-text-danger': statusUpdate === 'error'
          }"> {{ info }}</i>
    </label>
    <p class="help" v-if="$slots['info']">
      <slot name="info"></slot>
    </p>
  </div>
</template>

<script>
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'

export default {
  name: 'SettingsCheckbox',

  props: ['category_name', 'option_name'],

  data () {
    return {
      timerDelay: 2000,
      timerId: -1,

      // <empty>: default/no changes, 'success': update succesful, 'error': update failed
      statusUpdate: ''
    }
  },

  computed: {
    category () {
      return this.$store.state.settings.categories.find(elem => elem.name === this.category_name)
    },

    option () {
      if (!this.category) {
        return {}
      }
      return this.category.options.find(elem => elem.name === this.option_name)
    },

    value () {
      return this.option.value
    },

    info () {
      if (this.statusUpdate === 'success') {
        return '(setting saved)'
      } else if (this.statusUpdate === 'error') {
        return '(error saving setting)'
      }
      return ''
    }
  },

  methods: {
    set_update_timer () {
      if (this.timerId > 0) {
        window.clearTimeout(this.timerId)
        this.timerId = -1
      }

      this.statusUpdate = ''
      const newValue = this.$refs.settings_checkbox.checked
      if (newValue !== this.value) {
        this.timerId = window.setTimeout(this.update_setting, this.timerDelay)
      }
    },

    update_setting () {
      this.timerId = -1

      const newValue = this.$refs.settings_checkbox.checked
      console.log(this.$refs.settings_checkbox)
      if (newValue === this.value) {
        this.statusUpdate = ''
        return
      }

      const option = {
        category: this.category.name,
        name: this.option_name,
        value: newValue
      }
      webapi.settings_update(this.category.name, option).then(() => {
        this.$store.commit(types.UPDATE_SETTINGS_OPTION, option)
        this.statusUpdate = 'success'
      }).catch(() => {
        this.statusUpdate = 'error'
        this.$refs.settings_checkbox.checked = this.value
      }).finally(() => {
        this.timerId = window.setTimeout(this.clear_status, this.timerDelay)
      })
    },

    clear_status: function () {
      this.statusUpdate = ''
    }
  }
}
</script>

<style>
</style>
