<template>
  <div>
    <content-with-heading>
      <template #heading-left>
        <p class="title is-4" v-text="artist.name" />
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <a
            class="button is-small is-light is-rounded"
            @click="show_details_modal = true"
          >
            <mdicon class="icon" name="dots-horizontal" size="16" />
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <mdicon class="icon" name="shuffle" size="16" />
            <span v-text="$t('page.spotify.artist.shuffle')" />
          </a>
        </div>
      </template>
      <template #content>
        <p
          class="heading has-text-centered-mobile"
          v-text="$t('page.spotify.artist.album-count', { count: total })"
        />
        <list-item-album-spotify
          v-for="album in albums"
          :key="album.id"
          :item="album"
        />
        <VueEternalLoading v-if="offset < total" :load="load_next">
          <template #loading>
            <div class="columns is-centered">
              <div class="column has-text-centered">
                <mdicon class="icon mdi-spin" name="loading" />
              </div>
            </div>
          </template>
          <template #no-more>&nbsp;</template>
        </VueEternalLoading>
        <modal-dialog-artist-spotify
          :show="show_details_modal"
          :artist="artist"
          @close="show_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListItemAlbumSpotify from '@/components/ListItemAlbumSpotify.vue'
import ModalDialogArtistSpotify from '@/components/ModalDialogArtistSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import { VueEternalLoading } from '@ts-pro/vue-eternal-loading'
import store from '@/store'
import webapi from '@/webapi'

const PAGE_SIZE = 50

const dataObject = {
  load(to) {
    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(store.state.spotify.webapi_token)
    return Promise.all([
      spotifyApi.getArtist(to.params.id),
      spotifyApi.getArtistAlbums(to.params.id, {
        limit: PAGE_SIZE,
        offset: 0,
        include_groups: 'album,single',
        market: store.state.spotify.webapi_country
      })
    ])
  },

  set(vm, response) {
    vm.artist = response[0]
    vm.albums = []
    vm.total = 0
    vm.offset = 0
    vm.append_albums(response[1])
  }
}

export default {
  name: 'PageArtistSpotify',
  components: {
    ContentWithHeading,
    ListItemAlbumSpotify,
    ModalDialogArtistSpotify,
    VueEternalLoading
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
      albums: [],
      artist: {},
      offset: 0,
      show_details_modal: false,
      total: 0
    }
  },

  methods: {
    append_albums(data) {
      this.albums = this.albums.concat(data.items)
      this.total = data.total
      this.offset += data.limit
    },
    load_next({ loaded }) {
      const spotifyApi = new SpotifyWebApi()
      spotifyApi.setAccessToken(this.$store.state.spotify.webapi_token)
      spotifyApi
        .getArtistAlbums(this.artist.id, {
          include_groups: 'album,single',
          limit: PAGE_SIZE,
          offset: this.offset
        })
        .then((data) => {
          this.append_albums(data)
          loaded(data.items.length, PAGE_SIZE)
        })
    },
    play() {
      this.show_album_details_modal = false
      webapi.player_play_uri(this.artist.uri, true)
    }
  }
}
</script>

<style></style>
