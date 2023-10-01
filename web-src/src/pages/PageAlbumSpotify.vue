<template>
  <div class="fd-page">
    <content-with-hero>
      <template #heading-left>
        <h1 class="title is-5" v-text="album.name" />
        <h2 class="subtitle is-6 has-text-link has-text-weight-normal">
          <a
            class="has-text-link"
            @click="open_artist"
            v-text="album.artists[0].name"
          />
        </h2>
        <div class="buttons fd-is-centered-mobile mt-5">
          <a class="button is-small is-dark is-rounded" @click="play">
            <mdicon class="icon" name="shuffle" size="16" />
            <span v-text="$t('page.spotify.album.shuffle')" />
          </a>
          <a
            class="button is-small is-light is-rounded"
            @click="show_album_details_modal = true"
          >
            <mdicon class="icon" name="dots-horizontal" size="16" />
          </a>
        </div>
      </template>
      <template #heading-right>
        <cover-artwork
          :artwork_url="artwork_url"
          :artist="album.artist"
          :album="album.name"
          class="is-clickable fd-has-shadow fd-cover fd-cover-medium-image"
          @click="show_album_details_modal = true"
        />
      </template>
      <template #content>
        <p
          class="heading is-7 has-text-centered-mobile mt-5"
          v-text="
            $t('page.spotify.album.track-count', { count: album.tracks.total })
          "
        />
        <list-item-track-spotify
          v-for="(track, index) in album.tracks.items"
          :key="track.id"
          :track="track"
          :position="index"
          :context_uri="album.uri"
        >
          <template #actions>
            <a @click.prevent.stop="open_track_dialog(track)">
              <mdicon
                class="icon has-text-dark"
                name="dots-vertical"
                size="16"
              />
            </a>
          </template>
        </list-item-track-spotify>
        <modal-dialog-track-spotify
          :show="show_track_details_modal"
          :track="selected_track"
          :album="album"
          @close="show_track_details_modal = false"
        />
        <modal-dialog-album-spotify
          :show="show_album_details_modal"
          :album="album"
          @close="show_album_details_modal = false"
        />
      </template>
    </content-with-hero>
  </div>
</template>

<script>
import ContentWithHero from '@/templates/ContentWithHero.vue'
import CoverArtwork from '@/components/CoverArtwork.vue'
import ListItemTrackSpotify from '@/components/ListItemTrackSpotify.vue'
import ModalDialogAlbumSpotify from '@/components/ModalDialogAlbumSpotify.vue'
import ModalDialogTrackSpotify from '@/components/ModalDialogTrackSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import store from '@/store'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(store.state.spotify.webapi_token)
    return spotifyApi.getAlbum(to.params.id, {
      market: store.state.spotify.webapi_country
    })
  },

  set(vm, response) {
    vm.album = response
  }
}

export default {
  name: 'PageAlbum',
  components: {
    ContentWithHero,
    CoverArtwork,
    ListItemTrackSpotify,
    ModalDialogAlbumSpotify,
    ModalDialogTrackSpotify
  },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },
  beforeRouteUpdate(to, from, next) {
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  },

  data() {
    return {
      album: { artists: [{}], tracks: {} },

      show_track_details_modal: false,
      selected_track: {},

      show_album_details_modal: false
    }
  },

  computed: {
    artwork_url() {
      if (this.album.images && this.album.images.length > 0) {
        return this.album.images[0].url
      }
      return ''
    }
  },

  methods: {
    open_artist() {
      this.$router.push({
        name: 'music-spotify-artist',
        params: { id: this.album.artists[0].id }
      })
    },

    play() {
      this.show_details_modal = false
      webapi.player_play_uri(this.album.uri, true)
    },

    open_track_dialog(track) {
      this.selected_track = track
      this.show_track_details_modal = true
    }
  }
}
</script>

<style></style>
