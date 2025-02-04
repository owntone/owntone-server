<template>
  <div class="field">
    <label class="toggle">
      <div class="control is-flex is-align-content-center">
        <input
          :checked="modelValue"
          type="checkbox"
          class="toggle-checkbox"
          @change="$emit('update:modelValue', !modelValue)"
        />
        <div class="toggle-switch" />
        <slot name="label" />
      </div>
    </label>
    <div v-if="$slots.help" class="help">
      <slot name="help" />
    </div>
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
    width: 2.5rem;
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
    visibility: hidden;

    &:checked + .toggle-switch {
      background: var(--bulma-dark);

      &:before {
        left: 1.375rem;
      }
    }
  }
}
</style>
