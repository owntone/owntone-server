<template>
  <content-with-heading>
    <template slot="heading-left">
      <div class="title is-4">{{ playlist.name }}</div>
    </template>
    <template slot="heading-right">
      <div class="buttons is-centered">
        <a class="button is-small is-light is-rounded" @click="show_playlist_details_modal = true">
          <span class="icon"><i class="mdi mdi-dots-horizontal mdi-18px"></i></span>
        </a>
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
        </a>
      </div>
    </template>
    <template slot="content">
      <p class="heading has-text-centered-mobile">{{ playlist.tracks.total }} tracks</p>
      <spotify-list-item-track v-for="(item, index) in tracks" :key="item.track.id" :track="item.track" :album="item.track.album" :position="index" :context_uri="playlist.uri">
        <template slot="actions">
          <a @click="open_track_dialog(item.track)">
            <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
          </a>
        </template>
      </spotify-list-item-track>
      <infinite-loading v-if="offset < total" @infinite="load_next"><span slot="no-more">.</span></infinite-loading>
      <spotify-modal-dialog-track :show="show_track_details_modal" :track="selected_track" :album="selected_track.album" @close="show_track_details_modal = false" />
      <spotify-modal-dialog-playlist :show="show_playlist_details_modal" :playlist="playlist" @close="show_playlist_details_modal = false" />
    </template>
  </content-with-heading>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import SpotifyListItemTrack from '@/components/SpotifyListItemTrack'
import SpotifyModalDialogTrack from '@/components/SpotifyModalDialogTrack'
import SpotifyModalDialogPlaylist from '@/components/SpotifyModalDialogPlaylist'
import store from '@/store'
import webapi from '@/webapi'
import SpotifyWebApi from 'spotify-web-api-js'
import InfiniteLoading from 'vue-infinite-loading'

const playlistData = {
  load: function (to) {
    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(store.state.spotify.webapi_token)
    return Promise.all([
      spotifyApi.getPlaylist(to.params.playlist_id),
      spotifyApi.getPlaylistTracks(to.params.playlist_id, { limit: 50, offset: 0 })
    ])
  },

  set: function (vm, response) {
    vm.playlist = response[0]
    vm.tracks = []
    vm.total = 0
    vm.offset = 0
    vm.append_tracks(response[1])
  }
}

export default {
  name: 'SpotifyPagePlaylist',
  mixins: [LoadDataBeforeEnterMixin(playlistData)],
  components: { ContentWithHeading, SpotifyListItemTrack, SpotifyModalDialogTrack, SpotifyModalDialogPlaylist, InfiniteLoading },

  data () {
    return {
      playlist: { tracks: {} },
      tracks: [],
      total: 0,
      offset: 0,

      show_track_details_modal: false,
      selected_track: {},

      show_playlist_details_modal: false
    }
  },

  methods: {
    load_next: function ($state) {
      const spotifyApi = new SpotifyWebApi()
      spotifyApi.setAccessToken(this.$store.state.spotify.webapi_token)
      spotifyApi.getPlaylistTracks(this.playlist.id, { limit: 50, offset: this.offset }).then(data => {
        this.append_tracks(data, $state)
      })
    },

    append_tracks: function (data, $state) {
      this.tracks = this.tracks.concat(data.items)
      this.total = data.total
      this.offset += data.limit

      if ($state) {
        $state.loaded()
        if (this.offset >= this.total) {
          $state.complete()
        }
      }
    },

    play: function () {
      this.show_details_modal = false
      webapi.player_play_uri(this.playlist.uri, true)
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
