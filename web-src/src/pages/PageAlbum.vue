<template>
  <content-with-hero>
    <template slot="heading-left">
      <h1 class="title is-4 fd-has-margin-top">{{ album.name }}</h1>
      <h2 class="subtitle is-4 has-text-link has-text-weight-normal"><a class="has-text-link" @click="open_artist">{{ album.artist }}</a></h2>
      <p class="heading has-text-centered-mobile">{{ album.track_count }} tracks</p>

      <div class="buttons fd-is-centered-mobile fd-has-margin-top">
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
        </a>
        <a class="button is-small is-light is-rounded" @click="show_album_details_modal = true">
          <span class="icon"><i class="mdi mdi-dots-horizontal mdi-18px"></i></span>
        </a>
      </div>
    </template>
    <template slot="heading-right">
      <p class="image is-square fd-has-shadow">
        <cover-artwork
          :artwork_url="album.artwork_url"
          :artist="album.artist"
          :album="album.name" />
      </p>
    </template>
    <template slot="content">
      <list-item-track v-for="(track, index) in tracks" :key="track.id" :track="track" @click="play_track(index)">
        <template slot="actions">
          <a @click="open_dialog(track)">
            <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
          </a>
        </template>
      </list-item-track>
      <modal-dialog-track :show="show_details_modal" :track="selected_track" @close="show_details_modal = false" />
      <modal-dialog-album :show="show_album_details_modal" :album="album" @close="show_album_details_modal = false" />
    </template>
  </content-with-hero>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHero from '@/templates/ContentWithHero'
import ListItemTrack from '@/components/ListItemTrack'
import ModalDialogTrack from '@/components/ModalDialogTrack'
import ModalDialogAlbum from '@/components/ModalDialogAlbum'
import CoverArtwork from '@/components/CoverArtwork'
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
  mixins: [LoadDataBeforeEnterMixin(albumData)],
  components: { ContentWithHero, ListItemTrack, ModalDialogTrack, ModalDialogAlbum, CoverArtwork },

  data () {
    return {
      album: {},
      tracks: [],

      show_details_modal: false,
      selected_track: {},

      show_album_details_modal: false
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

    play_track: function (position) {
      webapi.player_play_uri(this.album.uri, false, position)
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
