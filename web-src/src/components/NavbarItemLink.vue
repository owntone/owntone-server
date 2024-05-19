<template>
  <a class="navbar-item" :href="href" @click.stop.prevent="open">
    <slot />
  </a>
</template>

<script>
import * as types from '@/store/mutation_types'

export default {
  name: 'NavbarItemLink',
  props: {
    to: { required: true, type: Object }
  },

  computed: {
    href() {
      return this.$router.resolve(this.to).href
    }
  },

  methods: {
    open() {
      if (this.$store.state.show_burger_menu) {
        this.$store.commit(types.SHOW_BURGER_MENU, false)
      }
      if (this.$store.state.show_player_menu) {
        this.$store.commit(types.SHOW_PLAYER_MENU, false)
      }
      this.$router.push(this.to)
    }
  }
}
</script>
