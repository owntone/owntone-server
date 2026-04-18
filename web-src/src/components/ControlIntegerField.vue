<template>
  <div class="field">
    <div class="control has-icons-left has-icons-right">
      <input
        ref="input"
        :value="modelValue"
        class="input"
        type="number"
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

<script setup>
const props = defineProps({
  modelValue: { type: Number, default: null },
  min: { type: Number, default: -Infinity },
  max: { type: Number, default: Infinity },
  step: { type: Number, default: 1 },
  placeholder: { type: String, default: '' }
})

const emit = defineEmits(['update:modelValue'])

const validate = (event) => {
  const { value } = event.target
  if (value === '' || value === '-') {
    return
  }
  event.target.value = Math.min(props.max, Math.max(props.min, Number(value)))
  emit('update:modelValue', event.target.valueAsNumber)
}

const out = (event) => {
  if (!event.target.valueAsNumber) {
    event.target.value = 0
    emit('update:modelValue', 0)
  }
}
</script>
