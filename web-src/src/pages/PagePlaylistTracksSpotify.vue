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
        <list-item-track-spotify
          v-for="track in tracks"
          :key="track.id"
          :item="track"
          :position="track.position"
          :context_uri="playlist.uri"
        />
        <VueEternalLoading v-if="offset < total" :load="load_next">
          <template #loading>
            <div class="columns is-centered">
              <div class="column has-text-centered">
                <mdicon class="icon mdi-spin" name="loading" />
              </div>
            </div>
          </template>
          <template #no-more>&nbsp;</template>
        </VueEternalLoading>
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
import SpotifyWebApi from 'spotify-web-api-js'
import { VueEternalLoading } from '@ts-pro/vue-eternal-loading'
import store from '@/store'
import webapi from '@/webapi'

const PAGE_SIZE = 50

const dataObject = {
  load(to) {
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
      spotifyApi.setAccessToken(this.$store.state.spotify.webapi_token)
      spotifyApi
        .getPlaylistTracks(this.playlist.id, {
          limit: PAGE_SIZE,
          market: store.state.spotify.webapi_country,
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
