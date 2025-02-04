<template>
  <div
    v-click-away="deactivate"
    class="dropdown"
    :class="{ 'is-active': active }"
  >
    <div class="dropdown-trigger">
      <button
        class="button"
        aria-haspopup="true"
        aria-controls="dropdown"
        @click="active = !active"
      >
        <span v-text="option.name" />
        <mdicon class="icon" name="chevron-down" size="16" />
      </button>
    </div>
    <div id="dropdown" class="dropdown-menu" role="menu">
      <div class="dropdown-content">
        <a
          v-for="o in options"
          :key="o.id"
          class="dropdown-item"
          :class="{ 'is-active': value === o.id }"
          @click="select(o)"
          v-text="o.name"
        />
      </div>
    </div>
  </div>
</template>

<script>
export default {
  name: 'ControlDropdown',
  props: {
    options: { required: true, type: Array },
    value: { required: true, type: [String, Number] }
  },
  emits: ['update:value'],

  data() {
    return {
      active: false
    }
  },

  computed: {
    option: {
      get() {
        return this.options.find((option) => option.id === this.value)
      }
    }
  },

  methods: {
    deactivate() {
      this.active = false
    },
    select(option) {
      this.active = false
      this.$emit('update:value', option.id)
    }
  }
}
</script>
