<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Featured Playlists</p>
      </template>
      <template slot="content">
        <spotify-list-item-playlist v-for="playlist in featured_playlists" :key="playlist.id" :playlist="playlist"></spotify-list-item-playlist>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import SpotifyListItemPlaylist from '@/components/SpotifyListItemPlaylist'
import store from '@/store'
import * as types from '@/store/mutation_types'
import SpotifyWebApi from 'spotify-web-api-js'

const browseData = {
  load: function (to) {
    if (store.state.spotify_featured_playlists.length > 0) {
      return Promise.resolve()
    }

    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(store.state.spotify.webapi_token)
    spotifyApi.getFeaturedPlaylists({ country: store.state.spotify.webapi_country, limit: 50 })
  },

  set: function (vm, response) {
    if (response) {
      store.commit(types.SPOTIFY_FEATURED_PLAYLISTS, response.playlists.items)
    }
  }
}

export default {
  name: 'SpotifyPageBrowseFeaturedPlaylists',
  mixins: [ LoadDataBeforeEnterMixin(browseData) ],
  components: { ContentWithHeading, TabsMusic, SpotifyListItemPlaylist },

  computed: {
    featured_playlists () {
      return this.$store.state.spotify_featured_playlists
    }
  }
}
</script>

<style>
</style>
