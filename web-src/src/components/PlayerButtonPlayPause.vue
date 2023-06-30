<template>
  <a :disabled="disabled" @click="toggle_play_pause">
    <mdicon
      :name="icon_name"
      :size="icon_size"
      :title="$t('player.button.' + icon_name)"
    />
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonPlayPause',

  props: {
    icon_size: {
      type: Number,
      default: 16
    },
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
    },

    icon_name() {
      if (!this.is_playing) {
        return 'play'
      } else if (this.is_pause_allowed) {
        return 'pause'
      }
      return 'stop'
    }
  },

  methods: {
    toggle_play_pause() {
      if (this.disabled) {
        if (this.show_disabled_message) {
          this.$store.dispatch('add_notification', {
            text: this.$t('server.empty-queue'),
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
