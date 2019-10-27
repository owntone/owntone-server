<template>
  <a class="navbar-item" :class="{ 'is-active': is_active }" @click.prevent="open_link()" :href="full_path()">
    <slot></slot>
  </a>
</template>

<script>
import * as types from '@/store/mutation_types'

export default {
  name: 'NavbarItemLink',
  props: [ 'to' ],

  computed: {
    is_active () {
      return this.$route.path.startsWith(this.to)
    }
  },

  methods: {
    open_link: function () {
      this.$store.commit(types.SHOW_BURGER_MENU, false)
      this.$router.push({ path: this.to })
    },

    full_path: function () {
      const resolved = this.$router.resolve(this.to)
      return resolved.href
    }
  }
}
</script>
