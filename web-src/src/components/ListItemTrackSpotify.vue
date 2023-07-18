<template>
  <div class="media is-align-items-center">
    <div
      class="media-content is-clipped"
      :class="{
        'is-clickable': track.is_playable,
        'fd-is-not-allowed': !track.is_playable
      }"
      @click="play"
    >
      <h1
        class="title is-6"
        :class="{ 'has-text-grey-light': !track.is_playable }"
        v-text="track.name"
      />
      <h2
        class="subtitle is-7"
        :class="{
          'has-text-grey': track.is_playable,
          'has-text-grey-light': !track.is_playable
        }"
        v-text="track.artists[0].name"
      />
      <h2 v-if="!track.is_playable" class="subtitle is-7">
        (<span v-text="$t('list.spotify.not-playable-track')" />
        <span
          v-if="track.restrictions && track.restrictions.reason"
          v-text="
            $t('list.spotify.restriction-reason', {
              reason: track.restrictions.reason
            })
          "
        />)
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
  name: 'ListItemTrackSpotify',
  props: ['track', 'position', 'context_uri'],
  methods: {
    play() {
      if (this.track.is_playable) {
        webapi.player_play_uri(this.context_uri, false, this.position)
      }
    }
  }
}
</script>

<style></style>
