<template>
  <content-with-heading>
    <template slot="heading-left">
      <p class="title is-4">{{ artist.name }}</p>
    </template>
    <template slot="content">
      <p class="heading has-text-centered-mobile">{{ total }} albums</p>
      <spotify-list-item-album v-for="album in albums" :key="album.id" :album="album"></spotify-list-item-album>
      <infinite-loading v-if="offset < total" @infinite="load_next"><span slot="no-more">.</span></infinite-loading>
    </template>
  </content-with-heading>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import SpotifyListItemAlbum from '@/components/SpotifyListItemAlbum'
import store from '@/store'
import SpotifyWebApi from 'spotify-web-api-js'
import InfiniteLoading from 'vue-infinite-loading'

const artistData = {
  load: function (to) {
    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(store.state.spotify.webapi_token)
    return Promise.all([
      spotifyApi.getArtist(to.params.artist_id),
      spotifyApi.getArtistAlbums(to.params.artist_id, { limit: 50, offset: 0, include_groups: 'album,single' })
    ])
  },

  set: function (vm, response) {
    vm.artist = response[0]

    vm.albums = []
    vm.total = 0
    vm.offset = 0
    vm.append_albums(response[1])
  }
}

export default {
  name: 'SpotifyPageArtist',
  mixins: [ LoadDataBeforeEnterMixin(artistData) ],
  components: { ContentWithHeading, SpotifyListItemAlbum, InfiniteLoading },

  data () {
    return {
      artist: {},
      albums: [],
      total: 0,
      offset: 0
    }
  },

  methods: {
    load_next: function ($state) {
      const spotifyApi = new SpotifyWebApi()
      spotifyApi.setAccessToken(this.$store.state.spotify.webapi_token)
      spotifyApi.getArtistAlbums(this.artist.id, { limit: 50, offset: this.offset, include_groups: 'album,single' }).then(data => {
        this.append_albums(data, $state)
      })
    },

    append_albums: function (data, $state) {
      this.albums = this.albums.concat(data.items)
      this.total = data.total
      this.offset += data.limit

      if ($state) {
        $state.loaded()
        if (this.offset >= this.total) {
          $state.complete()
        }
      }
    }
  }
}
</script>

<style>
</style>
