<template>
  <content-with-heading>
    <template #heading-left>
      <div class="title is-4">
        {{ playlist.name }}
      </div>
    </template>
    <template #heading-right>
      <div class="buttons is-centered">
        <a
          class="button is-small is-light is-rounded"
          @click="show_playlist_details_modal = true"
        >
          <span class="icon"
            ><i class="mdi mdi-dots-horizontal mdi-18px"
          /></span>
        </a>
        <a class="button is-small is-dark is-rounded" @click="play">
          <span class="icon"><i class="mdi mdi-shuffle" /></span>
          <span>Shuffle</span>
        </a>
      </div>
    </template>
    <template #content>
      <p class="heading has-text-centered-mobile">
        {{ playlist.tracks.total }} tracks
      </p>
      <spotify-list-item-track
        v-for="(item, index) in tracks"
        :key="item.track.id"
        :track="item.track"
        :album="item.track.album"
        :position="index"
        :context_uri="playlist.uri"
      >
        <template #actions>
          <a @click="open_track_dialog(item.track)">
            <span class="icon has-text-dark"
              ><i class="mdi mdi-dots-vertical mdi-18px"
            /></span>
          </a>
        </template>
      </spotify-list-item-track>
      <VueEternalLoading v-if="offset < total" :load="load_next">
        <template #no-more> . </template>
      </VueEternalLoading>
      <spotify-modal-dialog-track
        :show="show_track_details_modal"
        :track="selected_track"
        :album="selected_track.album"
        @close="show_track_details_modal = false"
      />
      <spotify-modal-dialog-playlist
        :show="show_playlist_details_modal"
        :playlist="playlist"
        @close="show_playlist_details_modal = false"
      />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import SpotifyListItemTrack from '@/components/SpotifyListItemTrack.vue'
import SpotifyModalDialogTrack from '@/components/SpotifyModalDialogTrack.vue'
import SpotifyModalDialogPlaylist from '@/components/SpotifyModalDialogPlaylist.vue'
import store from '@/store'
import webapi from '@/webapi'
import SpotifyWebApi from 'spotify-web-api-js'
import { VueEternalLoading } from '@ts-pro/vue-eternal-loading'

const PAGE_SIZE = 50

const dataObject = {
  load: function (to) {
    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(store.state.spotify.webapi_token)
    return Promise.all([
      spotifyApi.getPlaylist(to.params.playlist_id),
      spotifyApi.getPlaylistTracks(to.params.playlist_id, {
        limit: PAGE_SIZE,
        offset: 0
      })
    ])
  },

  set: function (vm, response) {
    vm.playlist = response[0]
    vm.tracks = []
    vm.total = 0
    vm.offset = 0
    vm.append_tracks(response[1])
  }
}

export default {
  name: 'SpotifyPagePlaylist',
  components: {
    ContentWithHeading,
    SpotifyListItemTrack,
    SpotifyModalDialogTrack,
    SpotifyModalDialogPlaylist,
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
      playlist: { tracks: {} },
      tracks: [],
      total: 0,
      offset: 0,

      show_track_details_modal: false,
      selected_track: {},

      show_playlist_details_modal: false
    }
  },

  methods: {
    load_next: function ({ loaded }) {
      const spotifyApi = new SpotifyWebApi()
      spotifyApi.setAccessToken(this.$store.state.spotify.webapi_token)
      spotifyApi
        .getPlaylistTracks(this.playlist.id, {
          limit: PAGE_SIZE,
          offset: this.offset
        })
        .then((data) => {
          this.append_tracks(data)
          loaded(data.items.length, PAGE_SIZE)
        })
    },

    append_tracks: function (data) {
      this.tracks = this.tracks.concat(data.items)
      this.total = data.total
      this.offset += data.limit
    },

    play: function () {
      this.show_details_modal = false
      webapi.player_play_uri(this.playlist.uri, true)
    },

    open_track_dialog: function (track) {
      this.selected_track = track
      this.show_track_details_modal = true
    }
  }
}
</script>

<style></style>
