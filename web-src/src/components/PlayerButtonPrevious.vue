<template>
  <a v-on:click="play_previous" :disabled="disabled">
    <span class="icon"><i class="mdi mdi-skip-backward"></i></span>
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonPrevious',

  computed: {
    disabled () {
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

<style>
</style>
