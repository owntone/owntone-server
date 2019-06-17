<template>
  <a v-on:click="toggle_play_pause">
    <span class="icon"><i class="mdi" v-bind:class="[icon_style, { 'mdi-play': !is_playing, 'mdi-pause': is_playing && is_pause_allowed, 'mdi-stop': is_playing && !is_pause_allowed }]"></i></span>
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonPlayPause',

  props: ['icon_style'],

  computed: {
    is_playing () {
      return this.$store.state.player.state === 'play'
    },

    is_pause_allowed () {
      return (this.$store.getters.now_playing &&
        !(this.$store.getters.now_playing.data_kind === 'url' && this.$store.state.player.item_length_ms <= 0) &&
        this.$store.getters.now_playing.data_kind !== 'pipe')
    }
  },

  methods: {
    toggle_play_pause: function () {
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

<style>
</style>
