<template>
  <div class="field">
    <div class="control has-icons-left has-icons-right">
      <input
        ref="input"
        :value="modelValue"
        class="input"
        type="number"
        inputmode="numeric"
        :placeholder="placeholder"
        :min="min"
        :max="max"
        :step="step"
        @input="validate"
        @blur="out"
      />
      <slot />
    </div>
  </div>
</template>

<script>
export default {
  name: 'ControlIntegerField',
  props: {
    modelValue: { type: Number, default: null },
    min: { type: Number, default: -Infinity },
    max: { type: Number, default: Infinity },
    step: { type: Number, default: 1 },
    placeholder: { type: String, default: '' }
  },
  emits: ['update:modelValue'],
  methods: {
    validate(event) {
      const { value } = event.target
      if (value === '' || value === '-') {
        return
      }
      event.target.value = Math.min(this.max, Math.max(this.min, Number(value)))
      this.$emit('update:modelValue', event.target.valueAsNumber)
    },
    out(event) {
      if (!event.target.valueAsNumber) {
        event.target.value = 0
        this.$emit('update:modelValue', 0)
      }
    }
  }
}
</script>
