<template>
  <content-with-heading>
    <template slot="heading-left">
      <div class="title is-4">{{ playlist.name }}</div>
    </template>
    <template slot="heading-right">
      <a class="button is-small is-dark is-rounded" @click="play">
        <span class="icon">
          <i class="mdi mdi-play"></i>
        </span>
        <span>Play</span>
      </a>
    </template>
    <template slot="content">
      <p class="heading has-text-centered-mobile">{{ playlist.tracks.total }} tracks</p>
      <spotify-list-item-track v-for="(item, index) in tracks" :key="item.track.id" :track="item.track" :album="item.track.album" :position="index" :context_uri="playlist.uri"></spotify-list-item-track>
      <infinite-loading v-if="offset < total" @infinite="load_next"><span slot="no-more">.</span></infinite-loading>
    </template>
  </content-with-heading>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import SpotifyListItemTrack from '@/components/SpotifyListItemTrack'
import store from '@/store'
import webapi from '@/webapi'
import SpotifyWebApi from 'spotify-web-api-js'
import InfiniteLoading from 'vue-infinite-loading'

const playlistData = {
  load: function (to) {
    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(store.state.spotify.webapi_token)
    return Promise.all([
      spotifyApi.getPlaylist(to.params.user_id, to.params.playlist_id),
      spotifyApi.getPlaylistTracks(to.params.user_id, to.params.playlist_id, { limit: 50, offset: 0 })
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
  mixins: [ LoadDataBeforeEnterMixin(playlistData) ],
  components: { ContentWithHeading, SpotifyListItemTrack, InfiniteLoading },

  data () {
    return {
      playlist: {},
      tracks: [],
      total: 0,
      offset: 0
    }
  },

  methods: {
    load_next: function ($state) {
      const spotifyApi = new SpotifyWebApi()
      spotifyApi.setAccessToken(this.$store.state.spotify.webapi_token)
      spotifyApi.getPlaylistTracks(this.playlist.owner.id, this.playlist.id, { limit: 50, offset: this.offset }).then(data => {
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
      webapi.queue_clear().then(() =>
        webapi.queue_add(this.playlist.uri).then(() =>
          webapi.player_play()
        )
      )
      this.show_details_modal = false
    }
  }
}
</script>

<style>
</style>
