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
        :button="{
          handler: play,
          icon: 'shuffle',
          key: 'actions.shuffle'
        }"
        :disabled="playlist.tracks.total === 0"
      />
    </template>
    <template #content>
      <list-tracks-spotify
        :context-uri="playlist.uri"
        :items="tracks"
        :load="load"
        :loaded="loaded"
      />
    </template>
  </content-with-heading>
  <modal-dialog-playlist-spotify
    :item="playlist"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListTracksSpotify from '@/components/ListTracksSpotify.vue'
import ModalDialogPlaylistSpotify from '@/components/ModalDialogPlaylistSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import queue from '@/api/queue'
import { useServicesStore } from '@/stores/services'

const PAGE_SIZE = 50

export default {
  name: 'PagePlaylistTracksSpotify',
  components: {
    ContentWithHeading,
    ControlButton,
    HeadingTitle,
    ListTracksSpotify,
    ModalDialogPlaylistSpotify
  },
  beforeRouteEnter(to, from, next) {
    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(useServicesStore().spotify.webapi_token)
    Promise.all([
      spotifyApi.getPlaylist(to.params.id),
      spotifyApi.getPlaylistTracks(to.params.id, {
        limit: PAGE_SIZE,
        market: useServicesStore().$state.spotify.webapi_country,
        offset: 0
      })
    ]).then(([playlist, tracks]) => {
      next((vm) => {
        vm.playlist = playlist
        vm.tracks = []
        vm.total = 0
        vm.offset = 0
        vm.appendTracks(tracks)
      })
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
            { count: this.playlist.tracks.total, key: 'data.playlists' }
          ],
          title: this.playlist.name
        }
      }
      return {}
    },
    loaded() {
      return !(this.offset < this.total)
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
            position += 1
            track.position = position
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
      queue.playUri(this.playlist.uri, true)
    },
    openDetails() {
      this.showDetailsModal = true
    }
  }
}
</script>
