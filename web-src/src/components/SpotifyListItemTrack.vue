<template>
  <div class="media">
    <div class="media-content fd-has-action is-clipped" @click="play">
      <h1 class="title is-6" :class="{ 'has-text-grey-light': track.is_playable === false }" v-text="track.name" />
      <h2 class="subtitle is-7" :class="{ 'has-text-grey': track.is_playable, 'has-text-grey-light': track.is_playable === false }">
        <b v-text="track.artists[0].name" />
      </h2>
      <h2 v-if="track.is_playable === false" class="subtitle is-7">
        (<span v-text="$t('list.spotify.not-playable-track')" />
        <span v-if="track.restrictions && track.restrictions.reason" v-text="$t('list.spotify.restriction-reason', { reason: track.restrictions.reason })" />)
      </h2>
    </div>
    <div class="media-right">
      <slot name="actions" />
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

<style></style>
