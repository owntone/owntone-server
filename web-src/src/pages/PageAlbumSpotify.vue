<template>
  <content-with-hero>
    <template #heading>
      <heading-hero :content="heading" />
    </template>
    <template #image>
      <control-image
        :url="album.images?.[0]?.url ?? ''"
        :caption="album.name"
        class="is-clickable is-medium"
        @click="openDetails"
      />
    </template>
    <template #content>
      <list-tracks-spotify :items="tracks" :context-uri="album.uri" />
    </template>
  </content-with-hero>
  <modal-dialog-album-spotify
    :item="album"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ContentWithHero from '@/templates/ContentWithHero.vue'
import ControlImage from '@/components/ControlImage.vue'
import HeadingHero from '@/components/HeadingHero.vue'
import ListTracksSpotify from '@/components/ListTracksSpotify.vue'
import ModalDialogAlbumSpotify from '@/components/ModalDialogAlbumSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import queue from '@/api/queue'
import services from '@/api/services'
import { useServicesStore } from '@/stores/services'

export default {
  name: 'PageAlbumSpotify',
  components: {
    ContentWithHero,
    ControlImage,
    HeadingHero,
    ListTracksSpotify,
    ModalDialogAlbumSpotify
  },
  beforeRouteEnter(to, from, next) {
    const spotifyApi = new SpotifyWebApi()
    services.spotify().then((data) => {
      spotifyApi.setAccessToken(data.webapi_token)
      spotifyApi
        .getAlbum(to.params.id, {
          market: useServicesStore().spotify.webapi_country
        })
        .then((album) => {
          next((vm) => {
            vm.album = album
          })
        })
    })
  },
  setup() {
    return { servicesStore: useServicesStore() }
  },
  data() {
    return {
      album: { artists: [{}], tracks: {} },
      showDetailsModal: false
    }
  },
  computed: {
    heading() {
      return {
        count: this.$t('data.tracks', { count: this.album.tracks.total }),
        handler: this.openArtist,
        subtitle: this.album.artists[0].name,
        title: this.album.name,
        actions: [
          { handler: this.play, icon: 'shuffle', key: 'actions.shuffle' },
          { handler: this.openDetails, icon: 'dots-horizontal' }
        ]
      }
    },
    tracks() {
      const { album } = this
      if (album.tracks.total) {
        return album.tracks.items.map((track) => ({ ...track, album }))
      }
      return []
    }
  },
  methods: {
    openArtist() {
      this.$router.push({
        name: 'music-spotify-artist',
        params: { id: this.album.artists[0].id }
      })
    },
    openDetails() {
      this.showDetailsModal = true
    },
    play() {
      this.showDetailsModal = false
      queue.playUri(this.album.uri, true)
    }
  }
}
</script>
