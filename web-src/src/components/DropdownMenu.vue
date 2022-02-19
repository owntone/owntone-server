<template>
  <div
    v-click-away="onClickOutside"
    class="dropdown"
    :class="{ 'is-active': is_active }"
  >
    <div class="dropdown-trigger">
      <button
        class="button"
        aria-haspopup="true"
        aria-controls="dropdown-menu"
        @click="is_active = !is_active"
      >
        <span>{{ modelValue }}</span>
        <span class="icon is-small">
          <i class="mdi mdi-chevron-down" aria-hidden="true" />
        </span>
      </button>
    </div>
    <div id="dropdown-menu" class="dropdown-menu" role="menu">
      <div class="dropdown-content">
        <a
          v-for="option in options"
          :key="option"
          class="dropdown-item"
          :class="{ 'is-active': modelValue === option }"
          @click="select(option)"
        >
          {{ option }}
        </a>
      </div>
    </div>
  </div>
</template>

<script>
export default {
  name: 'DropdownMenu',

  props: ['modelValue', 'options'],
  emits: ['update:modelValue'],

  data() {
    return {
      is_active: false
    }
  },

  methods: {
    onClickOutside(event) {
      this.is_active = false
    },

    select(option) {
      this.is_active = false
      this.$emit('update:modelValue', option)
    }
  }
}
</script>

<style></style>
