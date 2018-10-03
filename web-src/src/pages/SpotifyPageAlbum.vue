<template>
  <content-with-heading>
    <template slot="heading-left">
      <div class="title is-4">{{ album.name }}</div>
      <a class="title is-4 has-text-link has-text-weight-normal" @click="open_artist">{{ album.artists[0].name }}</a>
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
      <p class="heading has-text-centered-mobile">{{ album.tracks.total }} tracks</p>
      <spotify-list-item-track v-for="(track, index) in album.tracks.items" :key="track.id" :track="track" :position="index" :album="album" :context_uri="album.uri"></spotify-list-item-track>
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
  components: { ContentWithHeading, SpotifyListItemTrack },

  data () {
    return {
      album: {}
    }
  },

  methods: {
    open_artist: function () {
      this.$router.push({ path: '/music/spotify/artists/' + this.album.artists[0].id })
    },

    play: function () {
      webapi.queue_clear().then(() =>
        webapi.queue_add(this.album.uri).then(() =>
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
