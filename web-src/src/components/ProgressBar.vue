<template>
  <div v-if="width > 0" class="progress-bar mt-2" :style="{ width: width_percent }" />
</template>

<script>
export default {
  name: 'ProgressBar',
  props: ['max', 'value'],

  computed: {
    width () {
      if (this.value > 0 && this.max > 0) {
        return parseInt(this.value * 100 / this.max)
      }
      return 0
    },
    width_percent () {
      return this.width + '%'
    }
  }
}
</script>
