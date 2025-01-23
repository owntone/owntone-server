<template>
  <a class="navbar-item" :href="href" @click.stop.prevent="open">
    <slot />
  </a>
</template>

<script>
import { useUIStore } from '@/stores/ui'

export default {
  name: 'NavbarItemLink',
  props: {
    to: { required: true, type: Object }
  },

  setup() {
    return { uiStore: useUIStore() }
  },

  computed: {
    href() {
      return this.$router.resolve(this.to).href
    }
  },

  methods: {
    open() {
      if (this.uiStore.show_burger_menu) {
        this.uiStore.show_burger_menu = false
      }
      if (this.uiStore.show_player_menu) {
        this.uiStore.show_player_menu = false
      }
      this.$router.push(this.to)
    }
  }
}
</script>
