<template>
  <a v-on:click="toggle_play_stop">
    <span class="icon"><i class="mdi" v-bind:class="[icon_style, { 'mdi-play': is_stopped, 'mdi-stop': !is_stopped }]"></i></span>
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonPlayStop',

  props: ['icon_style'],

  computed: {
    is_stopped () {
      return this.$store.state.player.state === 'stop'
    }
  },

  methods: {
    toggle_play_stop: function () {
      if (this.is_stopped) {
        webapi.player_play()
      } else {
        webapi.player_stop()
      }
    }
  }
}
</script>

<style>
</style>
