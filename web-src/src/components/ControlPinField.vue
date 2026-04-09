<template>
  <div class="field">
    <div class="control">
      <input
        ref="input"
        v-model="value"
        class="input"
        type="text"
        inputmode="numeric"
        pattern="[\d]{4}"
        :placeholder="placeholder"
        @input="validate"
      />
    </div>
    <slot />
  </div>
</template>

<script>
export default {
  name: 'ControlPinField',
  props: {
    placeholder: { required: true, type: String }
  },
  emits: ['input'],
  data() {
    return { value: '' }
  },
  mounted() {
    setTimeout(() => {
      this.$refs.input.focus()
    }, 10)
  },
  methods: {
    validate(event) {
      const { validity } = event.target
      const invalid = validity.patternMismatch || validity.valueMissing
      this.$emit('input', this.value, invalid)
    }
  }
}
</script>
