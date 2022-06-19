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
      v-else-if="track.isItem"
      class="media"
      :class="{ 'with-progress': show_progress }"
      @click="play_track(index, track.item)"
    >
      <figure v-if="show_icon" class="media-left fd-has-action">
        <span class="icon"><mdicon name="file-outline" size="16" /></span>
      </figure>
      <div class="media-content fd-has-action is-clipped">
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
          <span class="icon has-text-dark"
            ><mdicon name="dots-vertical" size="16"
          /></span>
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
    play_track: function (position, track) {
      if (this.uris) {
        webapi.player_play_uri(this.uris, false, position)
      } else if (this.expression) {
        webapi.player_play_expression(this.expression, false, position)
      } else {
        webapi.player_play_uri(track.uri, false)
      }
    },

    open_dialog: function (track) {
      this.selected_track = track
      this.show_details_modal = true
    }
  }
}
</script>

<style></style>
