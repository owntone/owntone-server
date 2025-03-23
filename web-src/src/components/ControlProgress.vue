<template>
  <div class="is-flex">
    <svg :width="size" :height="size" viewBox="0 0 200 200">
      <defs>
        <circle id="circle" cx="50%" cy="50%" pathLength="1" r="90" />
      </defs>
      <use href="#circle" class="progress-base" />
      <use href="#circle" class="progress-bar" />
      <text x="50%" y="50%" class="is-size-1 progress-text" v-text="progress" />
    </svg>
  </div>
</template>

<script>
export default {
  name: 'ControlProgress',
  props: {
    size: { default: 36, type: Number },
    value: { default: 0, type: Number }
  },
  computed: {
    offset() {
      return 1 - this.value
    },
    progress() {
      return `${Math.round(this.value * 100)}%`
    }
  }
}
</script>

<style lang="scss" scoped>
.progress {
  fill: none;
  stroke-width: 15;
  &-base {
    @extend .progress;
    stroke: var(--bulma-border);
  }
  &-bar {
    @extend .progress;
    stroke: var(--bulma-text);
    stroke-linecap: round;
    stroke-dasharray: 1;
    stroke-dashoffset: v-bind(offset);
    transform: rotate(-90deg);
    transform-origin: center;
  }
  &-text {
    fill: var(--bulma-text);
    text-anchor: middle;
    dominant-baseline: central;
  }
}
</style>
