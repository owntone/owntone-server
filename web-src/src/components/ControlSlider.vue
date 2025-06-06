<template>
  <input
    :value="value"
    :disabled="disabled"
    class="slider"
    :class="{ 'is-inactive': disabled }"
    :max="max"
    type="range"
    @input="$emit('update:value', $event.target.valueAsNumber)"
  />
</template>

<script>
export default {
  name: 'ControlSlider',
  props: {
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

<style lang="scss" scoped>
@use 'bulma/sass/utilities/mixins';
@mixin thumb {
  -webkit-appearance: none;
  width: var(--th);
  height: var(--th);
  box-sizing: border-box;
  border-radius: 50%;
  background: var(--bulma-text);
}
@mixin thumb-inactive {
  box-sizing: border-box;
  background-color: var(--bulma-border);
}
@mixin track {
  height: calc(var(--sh));
  border-radius: calc(var(--sh) / 2);
  background: linear-gradient(
    90deg,
    var(--bulma-text) var(--sx),
    var(--bulma-border) var(--sx)
  );
}
@mixin track-inactive {
  background: linear-gradient(
    90deg,
    var(--bulma-border) var(--sx),
    var(--bulma-background) var(--sx)
  );
}
input[type='range'].slider {
  --ratio: v-bind(ratio);
  --sh: 0.25rem;
  --th: calc(var(--sh) * 4);
  background-color: transparent;
  @include mixins.mobile {
    --th: calc(var(--sh) * 5);
  }
  & {
    --sx: calc(var(--th) / 2 + (var(--ratio) * (100% - var(--th))));
    -webkit-appearance: none;
    min-width: 250px;
    height: calc(var(--sh) * 5);
    width: 100% !important;
    cursor: grab;
  }
  &:active {
    cursor: grabbing;
  }
  &::-webkit-slider-thumb {
    @include thumb;
    & {
      margin-top: calc((var(--th) - var(--sh)) / -2);
    }
  }
  &::-moz-range-thumb {
    @include thumb;
  }
  &::-webkit-slider-runnable-track {
    @include track;
  }
  &::-moz-range-track {
    @include track;
  }
  &.is-inactive {
    cursor: not-allowed;
    &::-webkit-slider-thumb {
      @include thumb-inactive;
    }
    &::-webkit-slider-runnable-track {
      @include track-inactive;
    }
    &::-moz-range-thumb {
      @include thumb-inactive;
    }
    &::-moz-range-track {
      @include track-inactive;
    }
  }
}
</style>
