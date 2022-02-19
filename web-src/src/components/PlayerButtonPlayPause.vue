<template>
  <a :disabled="disabled" @click="toggle_play_pause">
    <span class="icon"
      ><i
        class="mdi"
        :class="[
          icon_style,
          {
            'mdi-play': !is_playing,
            'mdi-pause': is_playing && is_pause_allowed,
            'mdi-stop': is_playing && !is_pause_allowed
          }
        ]"
    /></span>
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonPlayPause',

  props: {
    icon_style: String,
    show_disabled_message: Boolean
  },

  computed: {
    is_playing() {
      return this.$store.state.player.state === 'play'
    },

    is_pause_allowed() {
      return (
        this.$store.getters.now_playing &&
        this.$store.getters.now_playing.data_kind !== 'pipe'
      )
    },

    disabled() {
      return !this.$store.state.queue || this.$store.state.queue.count <= 0
    }
  },

  methods: {
    toggle_play_pause: function () {
      if (this.disabled) {
        if (this.show_disabled_message) {
          this.$store.dispatch('add_notification', {
            text: 'Queue is empty',
            type: 'info',
            topic: 'connection',
            timeout: 2000
          })
        }
        return
      }

      if (this.is_playing && this.is_pause_allowed) {
        webapi.player_pause()
      } else if (this.is_playing && !this.is_pause_allowed) {
        webapi.player_stop()
      } else {
        webapi.player_play()
      }
    }
  }
}
</script>

<style></style>
