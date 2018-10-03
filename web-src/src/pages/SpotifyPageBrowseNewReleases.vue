<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">New Releases</p>
      </template>
      <template slot="content">
        <spotify-list-item-album v-for="album in new_releases" :key="album.id" :album="album"></spotify-list-item-album>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import SpotifyListItemAlbum from '@/components/SpotifyListItemAlbum'
import store from '@/store'
import * as types from '@/store/mutation_types'
import SpotifyWebApi from 'spotify-web-api-js'

const browseData = {
  load: function (to) {
    if (store.state.spotify_new_releases.length > 0) {
      return Promise.resolve()
    }

    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(store.state.spotify.webapi_token)
    return spotifyApi.getNewReleases({ country: store.state.spotify.webapi_country, limit: 50 })
  },

  set: function (vm, response) {
    if (response) {
      store.commit(types.SPOTIFY_NEW_RELEASES, response.albums.items)
    }
  }
}

export default {
  name: 'SpotifyPageBrowseNewReleases',
  mixins: [ LoadDataBeforeEnterMixin(browseData) ],
  components: { ContentWithHeading, TabsMusic, SpotifyListItemAlbum },

  computed: {
    new_releases () {
      return this.$store.state.spotify_new_releases
    }
  }
}
</script>

<style>
</style>
