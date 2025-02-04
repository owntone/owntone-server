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
    @input="$emit('update:value', $event.target.valueAsNumber)"
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

<style lang="scss" scoped>
@use 'bulma/sass/utilities/mixins';

@mixin thumb {
  -webkit-appearance: none;
  width: var(--th);
  height: var(--th);
  box-sizing: border-box;
  border-radius: 50%;
  background: var(--bulma-light);
  border: 1px solid var(--bulma-grey-lighter);
  @media (prefers-color-scheme: dark) {
    background: var(--bulma-grey-lighter);
    border: 1px solid var(--bulma-grey-dark);
  }
}

@mixin thumb-inactive {
  box-sizing: border-box;
  background-color: var(--bulma-light);
  @media (prefers-color-scheme: dark) {
    background-color: var(--bulma-grey-dark);
    border: 1px solid var(--bulma-grey-darker);
  }
}

@mixin track {
  height: calc(var(--sh));
  border-radius: calc(var(--sh) / 2);
  background: linear-gradient(
    90deg,
    var(--bulma-dark) var(--sx),
    var(--bulma-grey-lighter) var(--sx)
  );
  @media (prefers-color-scheme: dark) {
    background: linear-gradient(
      90deg,
      var(--bulma-grey-lighter) var(--sx),
      var(--bulma-grey-dark) var(--sx)
    );
  }
}

@mixin track-inactive {
  background: linear-gradient(
    90deg,
    var(--bulma-grey-light) var(--sx),
    var(--bulma-light) var(--sx)
  );
  @media (prefers-color-scheme: dark) {
    background: linear-gradient(
      90deg,
      var(--bulma-grey-dark) var(--sx),
      var(--bulma-black-ter) var(--sx)
    );
  }
}

input[type='range'].slider {
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
    cursor: var(--cursor, not-allowed);
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
