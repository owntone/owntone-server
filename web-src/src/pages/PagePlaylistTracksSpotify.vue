<template>
  <div>
    <content-with-heading>
      <template #heading>
        <heading-title :content="heading" />
      </template>
      <template #actions>
        <control-button
          :button="{ handler: openDetails, icon: 'dots-horizontal' }"
        />
        <control-button
          :button="{
            handler: play,
            icon: 'shuffle',
            key: 'actions.shuffle'
          }"
          :disabled="playlist.tracks.total === 0"
        />
      </template>
      <template #content>
        <list-tracks-spotify :items="tracks" :context_uri="playlist.uri" />
        <vue-eternal-loading v-if="offset < total" :load="load">
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
        </vue-eternal-loading>
        <modal-dialog-playlist-spotify
          :item="playlist"
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
        market: useServicesStore().$state.spotify.webapi_country,
        offset: 0
      })
    ])
  },
  set(vm, response) {
    vm.playlist = response.shift()
    vm.tracks = []
    vm.total = 0
    vm.offset = 0
    vm.appendTracks(response.shift())
  }
}

export default {
  name: 'PagePlaylistTracksSpotify',
  components: {
    ContentWithHeading,
    ControlButton,
    HeadingTitle,
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
      showDetailsModal: false,
      total: 0,
      tracks: []
    }
  },
  computed: {
    heading() {
      if (this.playlist.name) {
        return {
          subtitle: [
            { count: this.playlist.tracks.total, key: 'count.playlists' }
          ],
          title: this.playlist.name
        }
      }
      return {}
    }
  },
  methods: {
    appendTracks(data) {
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
    load({ loaded }) {
      const spotifyApi = new SpotifyWebApi()
      spotifyApi.setAccessToken(this.servicesStore.spotify.webapi_token)
      spotifyApi
        .getPlaylistTracks(this.playlist.id, {
          limit: PAGE_SIZE,
          market: this.servicesStore.spotify.webapi_country,
          offset: this.offset
        })
        .then((data) => {
          this.appendTracks(data)
          loaded(data.items.length, PAGE_SIZE)
        })
    },
    play() {
      this.showDetailsModal = false
      webapi.player_play_uri(this.playlist.uri, true)
    },
    openDetails() {
      this.showDetailsModal = true
    }
  }
}
</script>
