<template>
  <div>
    <content-with-hero>
      <template #heading-left>
        <div class="title is-5" v-text="album.name" />
        <div class="subtitle is-6">
          <a @click="open_artist" v-text="album.artists[0].name" />
        </div>
        <div class="buttons is-centered-mobile mt-5">
          <a class="button is-small is-rounded" @click="play">
            <mdicon class="icon" name="shuffle" size="16" />
            <span v-text="$t('page.spotify.album.shuffle')" />
          </a>
          <a
            class="button is-small is-rounded"
            @click="show_details_modal = true"
          >
            <mdicon class="icon" name="dots-horizontal" size="16" />
          </a>
        </div>
      </template>
      <template #heading-right>
        <control-image
          :url="album.images?.[0]?.url ?? ''"
          :artist="album.artists[0].name"
          :album="album.name"
          class="is-clickable is-medium"
          @click="show_details_modal = true"
        />
      </template>
      <template #content>
        <div
          class="is-size-7 is-uppercase has-text-centered-mobile mt-5"
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
import ControlImage from '@/components/ControlImage.vue'
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
