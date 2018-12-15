<template>
  <content-with-heading>
    <template slot="heading-left">
      <div class="title is-4">{{ album.name }}</div>
      <a class="title is-4 has-text-link has-text-weight-normal" @click="open_artist">{{ album.artist }}</a>
    </template>
    <template slot="heading-right">
      <div class="buttons is-centered">
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
        </a>
        <!--
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon"><i class="mdi mdi-play"></i></span> <span>Play</span>
        </a>
        -->
      </div>
    </template>
    <template slot="content">
      <p class="heading has-text-centered-mobile">{{ album.track_count }} tracks</p>
      <list-item-track v-for="(track, index) in tracks" :key="track.id" :track="track" :position="index" :context_uri="album.uri">
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

const albumData = {
  load: function (to) {
    return Promise.all([
      webapi.library_album(to.params.album_id),
      webapi.library_album_tracks(to.params.album_id)
    ])
  },

  set: function (vm, response) {
    vm.album = response[0].data
    vm.tracks = response[1].data.items
  }
}

export default {
  name: 'PageAlbum',
  mixins: [ LoadDataBeforeEnterMixin(albumData) ],
  components: { ContentWithHeading, ListItemTrack, ModalDialogTrack },

  data () {
    return {
      album: {},
      tracks: [],

      show_details_modal: false,
      selected_track: {}
    }
  },

  methods: {
    open_artist: function () {
      this.show_details_modal = false
      this.$router.push({ path: '/music/artists/' + this.album.artist_id })
    },

    play: function () {
      webapi.player_play_uri(this.album.uri, true)
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
