<template>
  <div class="dropdown" :class="{ 'is-active': is_active }" v-click-outside="onClickOutside">
    <div class="dropdown-trigger">
      <button class="button" aria-haspopup="true" aria-controls="dropdown-menu" @click="is_active = !is_active">
        <span>{{ value }}</span>
        <span class="icon is-small">
            <i class="mdi mdi-chevron-down" aria-hidden="true"></i>
        </span>
      </button>
    </div>
    <div class="dropdown-menu" id="dropdown-menu" role="menu">
      <div class="dropdown-content">
        <a class="dropdown-item"
            v-for="option in options" :key="option"
            :class="{'is-active': value === option}"
            @click="select(option)">
          {{ option }}
        </a>
      </div>
    </div>
  </div>
</template>

<script>
export default {
  name: 'DropdownMenu',

  props: ['value', 'options'],

  data () {
    return {
      is_active: false
    }
  },

  methods: {
    onClickOutside (event) {
      this.is_active = false
    },

    select (option) {
      this.is_active = false
      this.$emit('input', option)
    }
  }
}
</script>

<style>
</style>
