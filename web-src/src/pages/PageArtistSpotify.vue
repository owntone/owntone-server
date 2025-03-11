<template>
  <div>
    <content-with-heading>
      <template #heading-left>
        <heading-title :content="heading" />
      </template>
      <template #heading-right>
        <control-button
          :button="{ handler: showDetails, icon: 'dots-horizontal' }"
        />
        <control-button
          :button="{ handler: play, icon: 'shuffle', key: 'actions.shuffle' }"
        />
      </template>
      <template #content>
        <list-albums-spotify :items="albums" />
        <vue-eternal-loading v-if="offset < total" :load="loadNext">
          <template #loading>
            <div class="columns is-centered">
              <div class="column has-text-centered">
                <mdicon class="icon mdi-spin" name="loading" />
              </div>
            </div>
          </template>
          <template #no-more>
            <br />
          </template>
          <template #no-results>
            <br />
          </template>
        </vue-eternal-loading>
        <modal-dialog-artist-spotify
          :item="artist"
          :show="showDetailsModal"
          @close="showDetailsModal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListAlbumsSpotify from '@/components/ListAlbumsSpotify.vue'
import ModalDialogArtistSpotify from '@/components/ModalDialogArtistSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import { VueEternalLoading } from '@ts-pro/vue-eternal-loading'
import { useServicesStore } from '@/stores/services'
import webapi from '@/webapi'

const PAGE_SIZE = 50

const dataObject = {
  load(to) {
    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(useServicesStore().spotify.webapi_token)
    return Promise.all([
      spotifyApi.getArtist(to.params.id),
      spotifyApi.getArtistAlbums(to.params.id, {
        include_groups: 'album,single',
        limit: PAGE_SIZE,
        market: useServicesStore().spotify.webapi_country,
        offset: 0
      })
    ])
  },
  set(vm, response) {
    vm.artist = response.shift()
    vm.albums = []
    vm.total = 0
    vm.offset = 0
    vm.appendAlbums(response.shift())
  }
}

export default {
  name: 'PageArtistSpotify',
  components: {
    ContentWithHeading,
    ControlButton,
    HeadingTitle,
    ListAlbumsSpotify,
    ModalDialogArtistSpotify,
    VueEternalLoading
  },
  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },
  setup() {
    return { servicesStore: useServicesStore() }
  },
  data() {
    return {
      albums: [],
      artist: {},
      offset: 0,
      showDetailsModal: false,
      total: 0
    }
  },
  computed: {
    heading() {
      return {
        subtitle: [{ count: this.total, key: 'count.albums' }],
        title: this.$t('artist.name')
      }
    }
  },
  methods: {
    appendAlbums(data) {
      this.albums = this.albums.concat(data.items)
      this.total = data.total
      this.offset += data.limit
    },
    loadNext({ loaded }) {
      const spotifyApi = new SpotifyWebApi()
      spotifyApi.setAccessToken(this.servicesStore.spotify.webapi_token)
      spotifyApi
        .getArtistAlbums(this.artist.id, {
          include_groups: 'album,single',
          limit: PAGE_SIZE,
          offset: this.offset
        })
        .then((data) => {
          this.appendAlbums(data)
          loaded(data.items.length, PAGE_SIZE)
        })
    },
    play() {
      this.showDetailsModal = false
      webapi.player_play_uri(this.artist.uri, true)
    },
    showDetails() {
      this.showDetailsModal = true
    }
  }
}
</script>
