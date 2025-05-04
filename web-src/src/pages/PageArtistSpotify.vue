<template>
  <content-with-heading>
    <template #heading>
      <heading-title :content="heading" />
    </template>
    <template #actions>
      <control-button
        :button="{ handler: openDetails, icon: 'dots-horizontal' }"
      />
      <control-button
        :button="{ handler: play, icon: 'shuffle', key: 'actions.shuffle' }"
      />
    </template>
    <template #content>
      <list-albums-spotify :items="albums" :load="load" :loaded="loaded" />
    </template>
  </content-with-heading>
  <modal-dialog-artist-spotify
    :item="artist"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListAlbumsSpotify from '@/components/ListAlbumsSpotify.vue'
import ModalDialogArtistSpotify from '@/components/ModalDialogArtistSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import queue from '@/api/queue'
import services from '@/api/services'
import { useServicesStore } from '@/stores/services'

const PAGE_SIZE = 50

export default {
  name: 'PageArtistSpotify',
  components: {
    ContentWithHeading,
    ControlButton,
    HeadingTitle,
    ListAlbumsSpotify,
    ModalDialogArtistSpotify
  },
  beforeRouteEnter(to, from, next) {
    services.spotify().then((data) => {
      const spotifyApi = new SpotifyWebApi()
      spotifyApi.setAccessToken(data.webapi_token)
      Promise.all([
        spotifyApi.getArtist(to.params.id),
        spotifyApi.getArtistAlbums(to.params.id, {
          include_groups: 'album,single',
          limit: PAGE_SIZE,
          market: useServicesStore().spotify.webapi_country,
          offset: 0
        })
      ]).then(([artist, albums]) => {
        next((vm) => {
          vm.artist = artist
          vm.albums = albums.items
          vm.total = albums.total
          vm.offset = albums.limit
        })
      })
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
        subtitle: [{ count: this.total, key: 'data.albums' }],
        title: this.artist.name
      }
    },
    loaded() {
      return !(this.offset < this.total)
    }
  },
  methods: {
    appendAlbums(data) {
      this.albums = this.albums.concat(data.items)
      this.total = data.total
      this.offset += data.limit
    },
    load({ loaded }) {
      api.spotify().then((data) => {
        const spotifyApi = new SpotifyWebApi()
        spotifyApi.setAccessToken(data.webapi_token)
        spotifyApi
          .getArtistAlbums(this.artist.id, {
            include_groups: 'album,single',
            limit: PAGE_SIZE,
            offset: this.offset
          })
          .then((albums) => {
            this.appendAlbums(albums)
            loaded(albums.items.length, PAGE_SIZE)
          })
      })
    },
    openDetails() {
      this.showDetailsModal = true
    },
    play() {
      this.showDetailsModal = false
      queue.playUri(this.artist.uri, true)
    }
  }
}
</script>
