<template>
  <a v-on:click="play_skip_fwd">
    <i v-if="is_skip_allowed">
      <span class="icon"><i class="mdi mdi-flip-h mdi-replay"></i></span>
    </i>
    <i v-else>
      <span class="icon has-text-grey-light"><i class="mdi mdi-flip-h mdi-replay"></i></span>
    </i>
  </a>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'PlayerButtonSkipFwd',
  props: [ 'when_ms' ],

  computed: {
    is_skip_allowed () {
      return this.$store.state.player.state !== 'stop' && this.$store.getters.now_playing && this.$store.getters.now_playing.data_kind !== 'url' && this.$store.getters.now_playing.data_kind !== 'pipe'
    }
  },

  methods: {
    play_skip_fwd: function () {
      if (this.is_skip_allowed) {
        webapi.player_seek(this.when_ms + 10000)
      }
    }
  }
}
</script>

<style>
</style>
