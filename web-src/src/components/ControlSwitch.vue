<template>
  <div class="field mb-0 is-flex is-align-items-center">
    <label class="toggle is-flex is-align-items-center">
      <div class="control is-flex is-align-items-center">
        <input
          :checked="modelValue"
          type="checkbox"
          class="toggle-checkbox is-hidden"
          @change="$emit('update:modelValue', !modelValue)"
        />
        <div class="toggle-switch" />
      </div>
      <slot name="label" />
    </label>
  </div>
  <div v-if="$slots.help" class="help mb-4">
    <slot name="help" />
  </div>
</template>

<script>
export default {
  name: 'ControlSwitch',
  props: {
    modelValue: Boolean
  },
  emits: ['update:modelValue']
}
</script>

<style lang="scss" scoped>
.toggle {
  cursor: pointer;
  display: inline-block;

  &-switch {
    display: inline-block;
    background: var(--bulma-grey-lighter);
    border-radius: 1rem;
    width: 2.25rem;
    height: 1.25rem;
    position: relative;
    vertical-align: middle;
    transition: background 0.25s;
    margin-right: 0.5rem;

    &:before {
      content: '';
      display: block;
      background: var(--bulma-white);
      border-radius: 50%;
      width: 1rem;
      height: 1rem;
      position: absolute;
      top: 0.125rem;
      left: 0.125rem;
      transition: left 0.25s;
    }
  }

  &:hover &-switch:before {
    background: var(--bulma-white);
  }

  &-checkbox {
    position: absolute;

    &:checked + .toggle-switch {
      background: var(--bulma-dark);

      &:before {
        left: 1.125rem;
      }
    }
  }
}
</style>
