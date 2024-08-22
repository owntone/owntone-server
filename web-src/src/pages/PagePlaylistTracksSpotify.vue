<template>
  <div>
    <content-with-heading>
      <template #heading-left>
        <div class="title is-4" v-text="playlist.name" />
      </template>
      <template #heading-right>
        <div class="buttons is-centered">
          <a
            class="button is-small is-light is-rounded"
            @click="show_playlist_details_modal = true"
          >
            <mdicon class="icon" name="dots-horizontal" size="16" />
          </a>
          <a class="button is-small is-dark is-rounded" @click="play">
            <mdicon class="icon" name="shuffle" size="16" />
            <span v-text="$t('page.spotify.playlist.shuffle')" />
          </a>
        </div>
      </template>
      <template #content>
        <p
          class="heading has-text-centered-mobile"
          v-text="
            $t('page.spotify.playlist.count', { count: playlist.tracks.total })
          "
        />
        <list-tracks-spotify :items="tracks" :context_uri="playlist.uri" />
        <VueEternalLoading v-if="offset < total" :load="load_next">
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
        </VueEternalLoading>
        <modal-dialog-playlist-spotify
          :item="playlist"
          :show="show_playlist_details_modal"
          @close="show_playlist_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListTracksSpotify from '@/components/ListTracksSpotify.vue'
import ModalDialogPlaylistSpotify from '@/components/ModalDialogPlaylistSpotify.vue'
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
      spotifyApi.getPlaylist(to.params.id),
      spotifyApi.getPlaylistTracks(to.params.id, {
        limit: PAGE_SIZE,
        market: useServicesStore().state.spotify.webapi_country,
        offset: 0
      })
    ])
  },

  set(vm, response) {
    vm.playlist = response.shift()
    vm.tracks = []
    vm.total = 0
    vm.offset = 0
    vm.append_tracks(response.shift())
  }
}

export default {
  name: 'PagePlaylistTracksSpotify',
  components: {
    ContentWithHeading,
    ListTracksSpotify,
    ModalDialogPlaylistSpotify,
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
      offset: 0,
      playlist: { tracks: {} },
      show_playlist_details_modal: false,
      total: 0,
      tracks: []
    }
  },

  methods: {
    append_tracks(data) {
      let position = Math.max(
        -1,
        ...this.tracks.map((item) => item.position).filter((item) => item)
      )
      // Filters out null tracks and adds a position to the playable tracks
      data.items.forEach((item) => {
        const { track } = item
        if (track) {
          if (track.is_playable) {
            track.position = ++position
          }
          this.tracks.push(track)
        }
      })
      this.total = data.total
      this.offset += data.limit
    },
    load_next({ loaded }) {
      const spotifyApi = new SpotifyWebApi()
      spotifyApi.setAccessToken(this.servicesStore.spotify.webapi_token)
      spotifyApi
        .getPlaylistTracks(this.playlist.id, {
          limit: PAGE_SIZE,
          market: this.servicesStore.spotify.webapi_country,
          offset: this.offset
        })
        .then((data) => {
          this.append_tracks(data)
          loaded(data.items.length, PAGE_SIZE)
        })
    },
    play() {
      this.show_details_modal = false
      webapi.player_play_uri(this.playlist.uri, true)
    }
  }
}
</script>

<style></style>
