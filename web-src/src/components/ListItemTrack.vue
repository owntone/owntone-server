<template>
  <div class="media">
    <div class="media-content fd-has-action is-clipped" v-on:click="play">
      <h1 class="title is-6">{{ track.title }}</h1>
      <h2 class="subtitle is-7 has-text-grey"><b>{{ track.artist }}</b></h2>
      <h2 class="subtitle is-7 has-text-grey">{{ track.album }}</h2>
    </div>
    <div class="media-right">
      <slot name="actions"></slot>
    </div>
  </div>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'ListItemTrack',
  components: {},

  props: ['track', 'position', 'context_uri'],

  data () {
    return {
    }
  },

  methods: {
    play: function () {
      webapi.player_play_uri(this.context_uri, false, this.position)
    }
  }
}
</script>

<style>
</style>
