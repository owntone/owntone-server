<template>
  <progress
    v-if="value > 0"
    class="progress is-info"
    :value="value"
    :max="max"
  />
</template>

<script>
export default {
  name: 'ProgressBar',
  props: ['max', 'value']
}
</script>

<style scoped>
.progress {
  height: 0.25rem;
}
</style>
