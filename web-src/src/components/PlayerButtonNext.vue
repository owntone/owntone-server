<template>
  <a :disabled="disabled" @click="play_next">
    <span class="icon"
      ><i class="mdi mdi-skip-forward" :class="icon_style"
    /></span>
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonNext',

  props: {
    icon_style: String
  },

  computed: {
    disabled() {
      return !this.$store.state.queue || this.$store.state.queue.count <= 0
    }
  },

  methods: {
    play_next: function () {
      if (this.disabled) {
        return
      }

      webapi.player_next()
    }
  }
}
</script>

<style></style>
