<template>
  <div class="media is-align-items-center">
    <div
      class="media-content is-clipped"
      :class="{
        'is-clickable': item.is_playable,
        'fd-is-not-allowed': !item.is_playable
      }"
      @click="play"
    >
      <h1
        class="title is-6"
        :class="{ 'has-text-grey-light': !item.is_playable }"
        v-text="item.name"
      />
      <h2
        class="subtitle is-7"
        :class="{
          'has-text-grey': item.is_playable,
          'has-text-grey-light': !item.is_playable
        }"
        v-text="item.artists[0].name"
      />
      <h2 v-if="!item.is_playable" class="subtitle is-7">
        (<span v-text="$t('list.spotify.not-playable-track')" />
        <span
          v-if="item.restrictions && item.restrictions.reason"
          v-text="
            $t('list.spotify.restriction-reason', {
              reason: item.restrictions.reason
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
  props: {
    context_uri: { required: true, type: String },
    item: { required: true, type: Object },
    position: { required: true, type: Number }
  },
  methods: {
    play() {
      if (this.item.is_playable) {
        webapi.player_play_uri(this.context_uri, false, this.position)
      }
    }
  }
}
</script>

<style></style>
