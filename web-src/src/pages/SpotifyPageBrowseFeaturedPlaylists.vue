<template>
  <div class="fd-page-with-tabs">
    <tabs-music></tabs-music>

    <content-with-heading>
      <template v-slot:heading-left>
        <p class="title is-4">Featured Playlists</p>
      </template>
      <template v-slot:content>
        <spotify-list-item-playlist v-for="playlist in featured_playlists" :key="playlist.id" :playlist="playlist">
          <template v-slot:actions>
            <a @click="open_playlist_dialog(playlist)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </spotify-list-item-playlist>
        <spotify-modal-dialog-playlist :show="show_playlist_details_modal" :playlist="selected_playlist" @close="show_playlist_details_modal = false" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import SpotifyListItemPlaylist from '@/components/SpotifyListItemPlaylist.vue'
import SpotifyModalDialogPlaylist from '@/components/SpotifyModalDialogPlaylist.vue'
import store from '@/store'
import * as types from '@/store/mutation_types'
import SpotifyWebApi from 'spotify-web-api-js'

const dataObject = {
  load: function (to) {
    if (store.state.spotify_featured_playlists.length > 0) {
      return Promise.resolve()
    }

    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(store.state.spotify.webapi_token)
    spotifyApi.getFeaturedPlaylists({ country: store.state.spotify.webapi_country, limit: 50 })
  },

  set: function (vm, response) {
    if (response) {
      store.commit(types.SPOTIFY_FEATURED_PLAYLISTS, response.playlists.items)
    }
  }
}

export default {
  name: 'SpotifyPageBrowseFeaturedPlaylists',
  components: { ContentWithHeading, TabsMusic, SpotifyListItemPlaylist, SpotifyModalDialogPlaylist },

  data () {
    return {
      show_playlist_details_modal: false,
      selected_playlist: {}
    }
  },

  computed: {
    featured_playlists () {
      return this.$store.state.spotify_featured_playlists
    }
  },

  methods: {
    open_playlist_dialog: function (playlist) {
      this.selected_playlist = playlist
      this.show_playlist_details_modal = true
    }
  },

  beforeRouteEnter (to, from, next) {
    dataObject.load(to).then((response) => {
      next(vm => dataObject.set(vm, response))
    })
  },
  beforeRouteUpdate (to, from, next) {
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  }
}
</script>

<style>
</style>
