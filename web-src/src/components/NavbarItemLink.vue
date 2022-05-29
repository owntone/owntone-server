<template>
  <a
    class="navbar-item"
    :class="{ 'is-active': is_active }"
    :href="full_path()"
    @click.stop.prevent="open_link()"
  >
    <slot />
  </a>
</template>

<script>
import * as types from '@/store/mutation_types'

export default {
  name: 'NavbarItemLink',
  props: {
    to: String,
    exact: Boolean
  },

  computed: {
    is_active() {
      if (this.exact) {
        return this.$route.path === this.to
      }
      return this.$route.path.startsWith(this.to)
    },

    show_player_menu: {
      get() {
        return this.$store.state.show_player_menu
      },
      set(value) {
        this.$store.commit(types.SHOW_PLAYER_MENU, value)
      }
    },

    show_burger_menu: {
      get() {
        return this.$store.state.show_burger_menu
      },
      set(value) {
        this.$store.commit(types.SHOW_BURGER_MENU, value)
      }
    }
  },

  methods: {
    open_link: function () {
      if (this.show_burger_menu) {
        this.$store.commit(types.SHOW_BURGER_MENU, false)
      }
      if (this.show_player_menu) {
        this.$store.commit(types.SHOW_PLAYER_MENU, false)
      }
      this.$router.push({ path: this.to })
    },

    full_path: function () {
      const resolved = this.$router.resolve(this.to)
      return resolved.href
    }
  }
}
</script>
