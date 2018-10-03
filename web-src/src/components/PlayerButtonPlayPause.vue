<template>
  <a v-on:click="toggle_play_pause">
    <span class="icon"><i class="mdi" v-bind:class="[icon_style, { 'mdi-play': !is_playing, 'mdi-pause': is_playing }]"></i></span>
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
    }
  },

  methods: {
    toggle_play_pause: function () {
      if (this.is_playing) {
        webapi.player_pause()
      } else {
        webapi.player_play()
      }
    }
  }
}
</script>

<style>
</style>
