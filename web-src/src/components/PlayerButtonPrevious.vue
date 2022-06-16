<template>
  <a :disabled="disabled" @click="play_previous">
    <span class="icon"><mdicon name="skip-backward" :size="icon_size" /></span>
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonPrevious',

  props: {
    icon_size: {
      type: Number,
      default: 16
    }
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
