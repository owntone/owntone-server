<template>
  <div class="field">
    <div class="control has-icons-left">
      <input
        ref="input"
        v-model="value"
        class="input"
        type="url"
        pattern="http[s]?://.+"
        required
        :placeholder="placeholder"
        :disabled="loading"
        @input="validate"
      />
      <mdicon class="icon is-left" :name="icon" size="16" />
    </div>
    <div v-if="help" class="help" v-text="help" />
  </div>
</template>

<script>
export default {
  name: 'ControlUrlField',
  props: {
    help: { default: '', type: String },
    icon: { required: true, type: String },
    loading: { default: false, type: Boolean },
    placeholder: { required: true, type: String }
  },
  emits: ['input'],
  data() {
    return {
      disabled: true,
      value: ''
    }
  },
  mounted() {
    setTimeout(() => {
      this.$refs.input.focus()
    }, 10)
  },
  methods: {
    validate(event) {
      const { validity } = event.target
      this.disabled = validity.patternMismatch || validity.valueMissing
      this.$emit('input', this.value, this.disabled)
    }
  }
}
</script>
