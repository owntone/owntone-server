<template>
  <content-with-heading>
    <template slot="heading-left">
      <div class="title is-4">{{ album.name }}</div>
      <a class="title is-4 has-text-link has-text-weight-normal" @click="open_artist">{{ album.artists[0].name }}</a>
    </template>
    <template slot="heading-right">
      <div class="buttons is-centered">
        <a class="button is-small is-light is-rounded" @click="show_album_details_modal = true">
          <span class="icon"><i class="mdi mdi-dots-horizontal mdi-18px"></i></span>
        </a>
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
        </a>
      </div>
    </template>
    <template slot="content">
      <p class="heading has-text-centered-mobile">{{ album.tracks.total }} tracks</p>
      <spotify-list-item-track v-for="(track, index) in album.tracks.items" :key="track.id" :track="track" :position="index" :album="album" :context_uri="album.uri">
        <template slot="actions">
          <a @click="open_track_dialog(track)">
            <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
          </a>
        </template>
      </spotify-list-item-track>
      <spotify-modal-dialog-track :show="show_track_details_modal" :track="selected_track" :album="album" @close="show_track_details_modal = false" />
      <spotify-modal-dialog-album :show="show_album_details_modal" :album="album" @close="show_album_details_modal = false" />
    </template>
  </content-with-heading>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import SpotifyListItemTrack from '@/components/SpotifyListItemTrack'
import SpotifyModalDialogTrack from '@/components/SpotifyModalDialogTrack'
import SpotifyModalDialogAlbum from '@/components/SpotifyModalDialogAlbum'
import store from '@/store'
import webapi from '@/webapi'
import SpotifyWebApi from 'spotify-web-api-js'

const albumData = {
  load: function (to) {
    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(store.state.spotify.webapi_token)
    return spotifyApi.getAlbum(to.params.album_id)
  },

  set: function (vm, response) {
    vm.album = response
  }
}

export default {
  name: 'PageAlbum',
  mixins: [ LoadDataBeforeEnterMixin(albumData) ],
  components: { ContentWithHeading, SpotifyListItemTrack, SpotifyModalDialogTrack, SpotifyModalDialogAlbum },

  data () {
    return {
      album: { artists: [{}], tracks: {} },

      show_track_details_modal: false,
      selected_track: {},

      show_album_details_modal: false
    }
  },

  methods: {
    open_artist: function () {
      this.$router.push({ path: '/music/spotify/artists/' + this.album.artists[0].id })
    },

    play: function () {
      this.show_details_modal = false
      webapi.player_play_uri(this.album.uri, true)
    },

    open_track_dialog: function (track) {
      this.selected_track = track
      this.show_track_details_modal = true
    }
  }
}
</script>

<style>
</style>
