<template>
  <content-with-heading>
    <template slot="heading-left">
      <p class="title is-4">{{ artist.name }}</p>
    </template>
    <template slot="heading-right">
      <div class="buttons is-centered">
        <a class="button is-small is-light is-rounded" @click="show_artist_details_modal = true">
          <span class="icon"><i class="mdi mdi-dots-horizontal mdi-18px"></i></span>
        </a>
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
        </a>
      </div>
    </template>
    <template slot="content">
      <p class="heading has-text-centered-mobile">{{ total }} albums</p>
      <spotify-list-item-album v-for="album in albums"
          :key="album.id"
          :album="album"
          @click="open_album(album)">
        <template slot="artwork" v-if="is_visible_artwork">
          <p class="image is-64x64 fd-has-shadow fd-has-action">
            <cover-artwork
              :artwork_url="artwork_url(album)"
              :artist="album.artist"
              :album="album.name"
              :maxwidth="64"
              :maxheight="64" />
          </p>
        </template>
        <template slot="actions">
          <a @click="open_dialog(album)">
            <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
          </a>
        </template>
      </spotify-list-item-album>
      <infinite-loading v-if="offset < total" @infinite="load_next"><span slot="no-more">.</span></infinite-loading>
      <spotify-modal-dialog-album :show="show_details_modal" :album="selected_album" @close="show_details_modal = false" />
      <spotify-modal-dialog-artist :show="show_artist_details_modal" :artist="artist" @close="show_artist_details_modal = false" />
    </template>
  </content-with-heading>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import SpotifyListItemAlbum from '@/components/SpotifyListItemAlbum'
import SpotifyModalDialogAlbum from '@/components/SpotifyModalDialogAlbum'
import SpotifyModalDialogArtist from '@/components/SpotifyModalDialogArtist'
import CoverArtwork from '@/components/CoverArtwork'
import store from '@/store'
import webapi from '@/webapi'
import SpotifyWebApi from 'spotify-web-api-js'
import InfiniteLoading from 'vue-infinite-loading'

const artistData = {
  load: function (to) {
    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(store.state.spotify.webapi_token)
    return Promise.all([
      spotifyApi.getArtist(to.params.artist_id),
      spotifyApi.getArtistAlbums(to.params.artist_id, { limit: 50, offset: 0, include_groups: 'album,single', market: store.state.spotify.webapi_country })
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
  mixins: [LoadDataBeforeEnterMixin(artistData)],
  components: { ContentWithHeading, SpotifyListItemAlbum, SpotifyModalDialogAlbum, SpotifyModalDialogArtist, InfiniteLoading, CoverArtwork },

  data () {
    return {
      artist: {},
      albums: [],
      total: 0,
      offset: 0,

      show_details_modal: false,
      selected_album: {},

      show_artist_details_modal: false
    }
  },

  computed: {
    is_visible_artwork () {
      return this.$store.getters.settings_option('webinterface', 'show_cover_artwork_in_album_lists').value
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
    },

    play: function () {
      this.show_details_modal = false
      webapi.player_play_uri(this.artist.uri, true)
    },

    open_album: function (album) {
      this.$router.push({ path: '/music/spotify/albums/' + album.id })
    },

    open_dialog: function (album) {
      this.selected_album = album
      this.show_details_modal = true
    },

    artwork_url: function (album) {
      if (album.images && album.images.length > 0) {
        return album.images[0].url
      }
      return ''
    }
  }
}
</script>

<style>
</style>
