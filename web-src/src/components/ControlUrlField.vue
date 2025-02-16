<template>
  <div class="field">
    <p class="control has-icons-left">
      <input
        ref="input"
        v-model="url"
        class="input"
        type="url"
        pattern="http[s]?://.+"
        required
        :placeholder="placeholder"
        :disabled="loading"
        @input="validate"
      />
      <mdicon class="icon is-left" :name="icon" size="16" />
    </p>
    <p v-if="help" class="help" v-text="help" />
  </div>
</template>

<script>
export default {
  name: 'ControlUrlField',
  props: {
    placeholder: { type: String, required: true },
    icon: { type: String, required: true },
    help: { type: String, default: '' },
    loading: { type: Boolean, default: false }
  },
  emits: ['url-changed'],
  data() {
    return {
      url: '',
      disabled: true
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
      this.$emit('url-changed', this.url, this.disabled)
    }
  }
}
</script>
