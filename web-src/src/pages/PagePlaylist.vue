<template>
  <content-with-heading>
    <template slot="heading-left">
      <div class="title is-4">{{ playlist.name }}</div>
    </template>
    <template slot="heading-right">
      <a class="button is-small is-dark is-rounded" @click="play">
        <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
      </a>
    </template>
    <template slot="content">
      <p class="heading has-text-centered-mobile">{{ tracks.length }} tracks</p>
      <list-item-track v-for="(track, index) in tracks" :key="track.id" :track="track" :position="index" :context_uri="playlist.uri">
        <template slot="actions">
          <a @click="open_dialog(track)">
            <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
          </a>
        </template>
      </list-item-track>
      <modal-dialog-track :show="show_details_modal" :track="selected_track" @close="show_details_modal = false" />
    </template>
  </content-with-heading>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import ListItemTrack from '@/components/ListItemTrack'
import ModalDialogTrack from '@/components/ModalDialogTrack'
import webapi from '@/webapi'

const playlistData = {
  load: function (to) {
    return Promise.all([
      webapi.library_playlist(to.params.playlist_id),
      webapi.library_playlist_tracks(to.params.playlist_id)
    ])
  },

  set: function (vm, response) {
    vm.playlist = response[0].data
    vm.tracks = response[1].data.items
  }
}

export default {
  name: 'PagePlaylist',
  mixins: [ LoadDataBeforeEnterMixin(playlistData) ],
  components: { ContentWithHeading, ListItemTrack, ModalDialogTrack },

  data () {
    return {
      playlist: {},
      tracks: [],

      show_details_modal: false,
      selected_track: {}
    }
  },

  methods: {
    play: function () {
      webapi.player_play_uri(this.playlist.uri, true)
    },

    open_dialog: function (track) {
      this.selected_track = track
      this.show_details_modal = true
    }
  }
}
</script>

<style>
</style>
