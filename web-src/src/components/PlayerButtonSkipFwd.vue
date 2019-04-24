<template>
  <a v-on:click="play_skip_fwd">
    <span class="icon"><i class="mdi mdi-flip-h mdi-replay"></i></span>
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonSkipFwd',
  props: [ 'when_ms' ],

  methods: {
    play_skip_fwd: function () {
      if (this.$store.state.player.state !== 'stop') {
        webapi.player_seek(this.when_ms + 10000)
      }
    }
  }
}
</script>

<style>
</style>
