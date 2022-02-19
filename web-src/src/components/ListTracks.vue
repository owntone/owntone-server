<template>
  <div
    v-for="(track, index) in tracks"
    :id="'index_' + track.title_sort.charAt(0).toUpperCase()"
    :key="track.id"
    class="media"
    :class="{ 'with-progress': show_progress }"
    @click="play_track(index, track)"
  >
    <figure v-if="show_icon" class="media-left fd-has-action">
      <span class="icon">
        <i class="mdi mdi-file-outline" />
      </span>
    </figure>
    <div class="media-content fd-has-action is-clipped">
      <h1
        class="title is-6"
        :class="{
          'has-text-grey':
            track.media_kind === 'podcast' && track.play_count > 0
        }"
      >
        {{ track.title }}
      </h1>
      <h2 class="subtitle is-7 has-text-grey">
        <b>{{ track.artist }}</b>
      </h2>
      <h2 class="subtitle is-7 has-text-grey">
        {{ track.album }}
      </h2>
      <progress-bar
        v-if="show_progress"
        :max="track.length_ms"
        :value="track.seek_ms"
      />
    </div>
    <div class="media-right">
      <a @click.prevent.stop="open_dialog(track)">
        <span class="icon has-text-dark"
          ><i class="mdi mdi-dots-vertical mdi-18px"
        /></span>
      </a>
    </div>
  </div>

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
