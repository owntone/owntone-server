<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">New Releases</p>
      </template>
      <template slot="content">
        <spotify-list-item-album v-for="album in new_releases" :key="album.id" :album="album">
          <template slot="actions">
            <a @click="open_album(album)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </spotify-list-item-album>
        <spotify-modal-dialog-album :show="show_album_details_modal" :album="selected_album" @close="show_album_details_modal = false" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import SpotifyListItemAlbum from '@/components/SpotifyListItemAlbum'
import SpotifyModalDialogAlbum from '@/components/SpotifyModalDialogAlbum'
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
  components: { ContentWithHeading, TabsMusic, SpotifyListItemAlbum, SpotifyModalDialogAlbum },

  data () {
    return {
      show_album_details_modal: false,
      selected_album: {}
    }
  },

  computed: {
    new_releases () {
      return this.$store.state.spotify_new_releases
    }
  },

  methods: {
    open_album: function (album) {
      this.selected_album = album
      this.show_album_details_modal = true
    }
  }
}
</script>

<style>
</style>
