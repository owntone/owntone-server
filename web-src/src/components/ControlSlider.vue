<template>
  <input
    :value="value"
    :disabled="disabled"
    class="slider"
    :class="{ 'is-inactive': disabled }"
    :max="max"
    type="range"
    :style="{
      '--ratio': ratio,
      '--cursor': $filters.cursor(cursor)
    }"
    @input="$emit('update:value', $event.target.value)"
  />
</template>

<script>
export default {
  name: 'ControlSlider',
  props: {
    cursor: { default: '', type: String },
    disabled: Boolean,
    max: { required: true, type: Number },
    value: { required: true, type: Number }
  },
  emits: ['update:value'],

  computed: {
    ratio() {
      return this.value / this.max
    }
  }
}
</script>
