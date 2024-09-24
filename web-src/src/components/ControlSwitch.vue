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
        <slot name="info" />
      </div>
    </label>
    <p v-if="$slots['help']" class="help notification">
      <slot name="help" />
    </p>
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

<style scoped>
.toggle {
  cursor: pointer;
  display: inline-block;
}

.toggle-switch {
  display: inline-block;
  background: var(--bulma-grey-lighter);
  border-radius: 1rem;
  min-width: 2.5rem;
  width: 2.5rem;
  height: 1.25rem;
  position: relative;
  vertical-align: middle;
  transition: background 0.25s;
  margin-right: 0.5rem;
}
.toggle-switch:before,
.toggle-switch:after {
  content: '';
}
.toggle-switch:before {
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
.toggle:hover .toggle-switch:before {
  background: var(--bulma-white);
}
.toggle-checkbox:checked + .toggle-switch {
  background: var(--bulma-dark);
}
.toggle-checkbox:checked + .toggle-switch:before {
  left: 1.375rem;
}

.toggle-checkbox {
  position: absolute;
  visibility: hidden;
}
</style>
