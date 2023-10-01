<template>
  <template v-for="track in tracks" :key="track.itemId">
    <div v-if="!track.isItem" class="mt-6 mb-5 py-2">
      <span
        :id="'index_' + track.groupKey"
        class="tag is-info is-light is-small has-text-weight-bold"
        v-text="track.groupKey"
      />
    </div>
    <div
      v-else
      class="media is-align-items-center"
      :class="{ 'with-progress': show_progress }"
      @click="play_track(track.item)"
    >
      <figure v-if="show_icon" class="media-left is-clickable">
        <mdicon class="icon" name="file-outline" size="16" />
      </figure>
      <div class="media-content is-clickable is-clipped">
        <h1
          class="title is-6"
          :class="{
            'has-text-grey':
              track.item.media_kind === 'podcast' && track.item.play_count > 0
          }"
          v-text="track.item.title"
        />
        <h2 class="subtitle is-7 has-text-grey" v-text="track.item.artist" />
        <h2 class="subtitle is-7 has-text-grey" v-text="track.item.album" />
        <progress-bar
          v-if="show_progress"
          :max="track.item.length_ms"
          :value="track.item.seek_ms"
        />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(track.item)">
          <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-track
      :show="show_details_modal"
      :track="selected_track"
      @close="show_details_modal = false"
      @play-count-changed="$emit('play-count-changed')"
    />
  </teleport>
</template>

<script>
import ModalDialogTrack from '@/components/ModalDialogTrack.vue'
import ProgressBar from '@/components/ProgressBar.vue'
import webapi from '@/webapi'

export default {
  name: 'ListTracks',
  components: { ModalDialogTrack, ProgressBar },

  props: ['tracks', 'uris', 'expression', 'show_progress', 'show_icon'],
  emits: ['play-count-changed'],

  data() {
    return {
      show_details_modal: false,
      selected_track: {}
    }
  },

  methods: {
    play_track(track) {
      if (this.uris) {
        webapi.player_play_uri(
          this.uris,
          false,
          this.tracks.items.indexOf(track)
        )
      } else if (this.expression) {
        webapi.player_play_expression(
          this.expression,
          false,
          this.tracks.items.indexOf(track)
        )
      } else {
        webapi.player_play_uri(track.uri, false)
      }
    },

    open_dialog(track) {
      this.selected_track = track
      this.show_details_modal = true
    }
  }
}
</script>

<style></style>
