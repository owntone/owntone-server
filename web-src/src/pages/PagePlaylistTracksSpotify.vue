<template>
  <div class="fd-page">
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
        <list-item-track-spotify
          v-for="track in tracks"
          :key="track.id"
          :track="track"
          :position="track.position"
          :context_uri="playlist.uri"
        >
          <template #actions>
            <a @click.prevent.stop="open_track_dialog(track)">
              <mdicon
                class="icon has-text-dark"
                name="dots-vertical"
                size="16"
              />
            </a>
          </template>
        </list-item-track-spotify>
        <VueEternalLoading v-if="offset < total" :load="load_next">
          <template #no-more> . </template>
        </VueEternalLoading>
        <modal-dialog-track-spotify
          :show="show_track_details_modal"
          :track="selected_track"
          :album="selected_track.album"
          @close="show_track_details_modal = false"
        />
        <modal-dialog-playlist-spotify
          :show="show_playlist_details_modal"
          :playlist="playlist"
          @close="show_playlist_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListItemTrackSpotify from '@/components/ListItemTrackSpotify.vue'
import ModalDialogPlaylistSpotify from '@/components/ModalDialogPlaylistSpotify.vue'
import ModalDialogTrackSpotify from '@/components/ModalDialogTrackSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import store from '@/store'
import { VueEternalLoading } from '@ts-pro/vue-eternal-loading'
import webapi from '@/webapi'

const PAGE_SIZE = 50

const dataObject = {
  load: function (to) {
    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(store.state.spotify.webapi_token)
    return Promise.all([
      spotifyApi.getPlaylist(to.params.id),
      spotifyApi.getPlaylistTracks(to.params.id, {
        limit: PAGE_SIZE,
        offset: 0,
        market: store.state.spotify.webapi_country
      })
    ])
  },

  set(vm, response) {
    vm.playlist = response[0]
    vm.tracks = []
    vm.total = 0
    vm.offset = 0
    vm.append_tracks(response[1])
  }
}

export default {
  name: 'PagePlaylistTracksSpotify',
  components: {
    ContentWithHeading,
    ListItemTrackSpotify,
    ModalDialogPlaylistSpotify,
    ModalDialogTrackSpotify,
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
    load_next({ loaded }) {
      const spotifyApi = new SpotifyWebApi()
      spotifyApi.setAccessToken(this.$store.state.spotify.webapi_token)
      spotifyApi
        .getPlaylistTracks(this.playlist.id, {
          limit: PAGE_SIZE,
          offset: this.offset,
          market: store.state.spotify.webapi_country
        })
        .then((data) => {
          this.append_tracks(data)
          loaded(data.items.length, PAGE_SIZE)
        })
    },

    append_tracks(data) {
      let position = Math.max(
        -1,
        ...this.tracks.map((item) => item.position).filter((item) => item)
      )
      // Filters out null tracks and adds a position to the playable tracks
      data.items.forEach((item) => {
        const track = item.track
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

    play() {
      this.show_details_modal = false
      webapi.player_play_uri(this.playlist.uri, true)
    },

    open_track_dialog(track) {
      this.selected_track = track
      this.show_track_details_modal = true
    }
  }
}
</script>

<style></style>
