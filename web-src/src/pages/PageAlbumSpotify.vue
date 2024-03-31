<template>
  <div>
    <content-with-hero>
      <template #heading-left>
        <h1 class="title is-5" v-text="album.name" />
        <h2 class="subtitle is-6 has-text-link">
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
            @click="show_details_modal = true"
          >
            <mdicon class="icon" name="dots-horizontal" size="16" />
          </a>
        </div>
      </template>
      <template #heading-right>
        <cover-artwork
          :artwork_url="artwork_url(album)"
          :artist="album.artists[0].name"
          :album="album.name"
          class="is-clickable fd-has-shadow fd-cover fd-cover-medium-image"
          @click="show_details_modal = true"
        />
      </template>
      <template #content>
        <p
          class="heading has-text-centered-mobile mt-5"
          v-text="
            $t('page.spotify.album.track-count', { count: album.tracks.total })
          "
        />
        <list-tracks-spotify :items="tracks" :context_uri="album.uri" />
        <modal-dialog-album-spotify
          :item="album"
          :show="show_details_modal"
          @close="show_details_modal = false"
        />
      </template>
    </content-with-hero>
  </div>
</template>

<script>
import ContentWithHero from '@/templates/ContentWithHero.vue'
import CoverArtwork from '@/components/CoverArtwork.vue'
import ListTracksSpotify from '@/components/ListTracksSpotify.vue'
import ModalDialogAlbumSpotify from '@/components/ModalDialogAlbumSpotify.vue'
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
  name: 'PageAlbumSpotify',
  components: {
    ContentWithHero,
    CoverArtwork,
    ListTracksSpotify,
    ModalDialogAlbumSpotify
  },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },

  data() {
    return {
      album: { artists: [{}], tracks: {} },
      show_details_modal: false
    }
  },

  computed: {
    tracks() {
      const { album } = this
      if (album.tracks.total) {
        return album.tracks.items.map((track) => ({ ...track, album }))
      }
      return {}
    }
  },
  methods: {
    artwork_url(album) {
      return album.images?.[0]?.url ?? ''
    },
    open_artist() {
      this.$router.push({
        name: 'music-spotify-artist',
        params: { id: this.album.artists[0].id }
      })
    },
    play() {
      this.show_details_modal = false
      webapi.player_play_uri(this.album.uri, true)
    }
  }
}
</script>

<style></style>
