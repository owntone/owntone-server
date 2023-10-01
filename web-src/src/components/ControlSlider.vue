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
  props: ['value', 'max', 'disabled', 'cursor'],
  emits: ['update:value'],

  computed: {
    ratio() {
      return this.value / this.max
    }
  }
}
</script>
