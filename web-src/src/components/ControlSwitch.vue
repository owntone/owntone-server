<template>
  <label class="toggle">
    <input
      :checked="modelValue"
      type="checkbox"
      class="toggle-checkbox is-rounded mr-2"
      @change="$emit('update:modelValue', !modelValue)"
    />
    <div class="toggle-switch"></div>
    <slot name="label" class="toggle-label" />
  </label>
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
  background: var(--bulma-grey-light);
  border-radius: 1rem;
  width: 2.75rem;
  height: 1.5rem;
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
  top: 0.25rem;
  left: 0.25rem;
  transition: left 0.25s;
}
.toggle:hover .toggle-switch:before {
  background: var(--bulma-white);
}
.toggle-checkbox:checked + .toggle-switch {
  background: var(--bulma-primary);
}
.toggle-checkbox:checked + .toggle-switch:before {
  left: 1.5rem;
}

.toggle-checkbox {
  position: absolute;
  visibility: hidden;
}
</style>
