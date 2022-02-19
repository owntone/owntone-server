<template>
  <content-with-hero>
    <template v-slot:heading-left>
      <h1 class="title is-5">{{ album.name }}</h1>
      <h2 class="subtitle is-6 has-text-link has-text-weight-normal"><a class="has-text-link" @click="open_artist">{{ album.artists[0].name }}</a></h2>

      <div class="buttons fd-is-centered-mobile fd-has-margin-top">
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon"><i class="mdi mdi-shuffle"></i></span> <span>Shuffle</span>
        </a>
        <a class="button is-small is-light is-rounded" @click="show_album_details_modal = true">
          <span class="icon"><i class="mdi mdi-dots-horizontal mdi-18px"></i></span>
        </a>
      </div>
    </template>

    <template v-slot:heading-right>
      <p class="image is-square fd-has-shadow fd-has-action">
        <cover-artwork
          :artwork_url="artwork_url"
          :artist="album.artist"
          :album="album.name"
          @click="show_album_details_modal = true" />
      </p>
    </template>

    <template v-slot:content>
      <p class="heading is-7 has-text-centered-mobile fd-has-margin-top">{{ album.tracks.total }} tracks</p>
      <spotify-list-item-track v-for="(track, index) in album.tracks.items" :key="track.id" :track="track" :position="index" :album="album" :context_uri="album.uri">
        <template v-slot:actions>
          <a @click="open_track_dialog(track)">
            <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
          </a>
        </template>
      </spotify-list-item-track>
      <spotify-modal-dialog-track :show="show_track_details_modal" :track="selected_track" :album="album" @close="show_track_details_modal = false" />
      <spotify-modal-dialog-album :show="show_album_details_modal" :album="album" @close="show_album_details_modal = false" />
    </template>
  </content-with-hero>
</template>

<script>
import ContentWithHero from '@/templates/ContentWithHero.vue'
import SpotifyListItemTrack from '@/components/SpotifyListItemTrack.vue'
import SpotifyModalDialogTrack from '@/components/SpotifyModalDialogTrack.vue'
import SpotifyModalDialogAlbum from '@/components/SpotifyModalDialogAlbum.vue'
import CoverArtwork from '@/components/CoverArtwork.vue'
import store from '@/store'
import webapi from '@/webapi'
import SpotifyWebApi from 'spotify-web-api-js'

const dataObject = {
  load: function (to) {
    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(store.state.spotify.webapi_token)
    return spotifyApi.getAlbum(to.params.album_id, { market: store.state.spotify.webapi_country })
  },

  set: function (vm, response) {
    vm.album = response
  }
}

export default {
  name: 'PageAlbum',
  components: { ContentWithHero, SpotifyListItemTrack, SpotifyModalDialogTrack, SpotifyModalDialogAlbum, CoverArtwork },

  data () {
    return {
      album: { artists: [{}], tracks: {} },

      show_track_details_modal: false,
      selected_track: {},

      show_album_details_modal: false
    }
  },

  computed: {
    artwork_url: function () {
      if (this.album.images && this.album.images.length > 0) {
        return this.album.images[0].url
      }
      return ''
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
  },

  beforeRouteEnter (to, from, next) {
    dataObject.load(to).then((response) => {
      next(vm => dataObject.set(vm, response))
    })
  },
  beforeRouteUpdate (to, from, next) {
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  }
}
</script>

<style>
</style>
