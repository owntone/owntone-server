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
    help: { default: '', type: String },
    icon: { required: true, type: String },
    loading: { default: false, type: Boolean },
    placeholder: { required: true, type: String }
  },
  emits: ['url-changed'],
  data() {
    return {
      disabled: true,
      url: ''
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
