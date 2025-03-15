<template>
  <div>
    <content-with-hero>
      <template #heading>
        <heading-hero :content="heading" />
      </template>
      <template #image>
        <control-image
          :url="album.images?.[0]?.url ?? ''"
          :artist="album.artists[0].name"
          :album="album.name"
          class="is-clickable is-medium"
          @click="openDetails"
        />
      </template>
      <template #content>
        <list-tracks-spotify :items="tracks" :context_uri="album.uri" />
        <modal-dialog-album-spotify
          :item="album"
          :show="showDetailsModal"
          @close="showDetailsModal = false"
        />
      </template>
    </content-with-hero>
  </div>
</template>

<script>
import ContentWithHero from '@/templates/ContentWithHero.vue'
import ControlImage from '@/components/ControlImage.vue'
import HeadingHero from '@/components/HeadingHero.vue'
import ListTracksSpotify from '@/components/ListTracksSpotify.vue'
import ModalDialogAlbumSpotify from '@/components/ModalDialogAlbumSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import { useServicesStore } from '@/stores/services'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(useServicesStore().spotify.webapi_token)
    return spotifyApi.getAlbum(to.params.id, {
      market: useServicesStore().spotify.webapi_country
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
    ControlImage,
    HeadingHero,
    ListTracksSpotify,
    ModalDialogAlbumSpotify
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
      album: { artists: [{}], tracks: {} },
      showDetailsModal: false
    }
  },
  computed: {
    heading() {
      return {
        count: this.$t('count.tracks', { count: this.album.tracks.total }),
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
      return {}
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
      webapi.player_play_uri(this.album.uri, true)
    }
  }
}
</script>
