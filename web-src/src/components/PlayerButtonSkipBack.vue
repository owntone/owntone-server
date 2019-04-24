<template>
  <a v-on:click="play_skip_back">
    <span class="icon"><i class="mdi mdi-replay"></i></span>
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonSkipBack',
  props: [ 'when_ms' ],

  methods: {
    play_skip_back: function () {
      if (this.$store.state.player.state !== 'stop') {
        webapi.player_seek(this.when_ms - 10000)
      }
    }
  }
}
</script>

<style>
</style>
