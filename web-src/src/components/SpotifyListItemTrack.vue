<template>
  <div class="media">
    <div class="media-content fd-has-action is-clipped" v-on:click="play">
      <h1 class="title is-6">{{ track.name }}</h1>
      <h2 class="subtitle is-7 has-text-grey"><b>{{ track.artists[0].name }}</b></h2>
    </div>
    <div class="media-right">
      <slot name="actions"></slot>
    </div>
  </div>
</template>

<script>
import webapi from '@/webapi'

export default {
  name: 'SpotifyListItemTrack',

  props: ['track', 'position', 'album', 'context_uri'],

  methods: {
    play: function () {
      webapi.player_play_uri(this.context_uri, false, this.position)
    }
  }
}
</script>

<style>
</style>
