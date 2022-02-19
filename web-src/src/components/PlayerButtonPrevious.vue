<template>
  <a :disabled="disabled" @click="play_previous">
    <span class="icon"
      ><i class="mdi mdi-skip-backward" :class="icon_style"
    /></span>
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonPrevious',

  props: {
    icon_style: String
  },

  computed: {
    disabled() {
      return !this.$store.state.queue || this.$store.state.queue.count <= 0
    }
  },

  methods: {
    play_previous: function () {
      if (this.disabled) {
        return
      }

      webapi.player_previous()
    }
  }
}
</script>

<style></style>
