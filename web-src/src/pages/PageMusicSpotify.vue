<template>
  <div class="fd-page-with-tabs">
    <tabs-music />
    <!-- New Releases -->
    <content-with-heading>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.spotify.music.new-releases')" />
      </template>
      <template #content>
        <list-item-album-spotify
          v-for="album in new_releases"
          :key="album.id"
          :item="album"
        >
          <template #actions>
            <a @click.prevent.stop="open_album_dialog(album)">
              <mdicon
                class="icon has-text-dark"
                name="dots-vertical"
                size="16"
              />
            </a>
          </template>
        </list-item-album-spotify>
        <modal-dialog-album-spotify
          :show="show_album_details_modal"
          :album="selected_album"
          @close="show_album_details_modal = false"
        />
      </template>
      <template #footer>
        <nav class="level">
          <p class="level-item">
            <router-link
              :to="{ name: 'music-spotify-new-releases' }"
              class="button is-light is-small is-rounded"
              >{{ $t('page.spotify.music.show-more') }}</router-link
            >
          </p>
        </nav>
      </template>
    </content-with-heading>
    <!-- Featured Playlists -->
    <content-with-heading>
      <template #heading-left>
        <p
          class="title is-4"
          v-text="$t('page.spotify.music.featured-playlists')"
        />
      </template>
      <template #content>
        <list-item-playlist-spotify
          v-for="playlist in featured_playlists"
          :key="playlist.id"
          :item="playlist"
        >
          <template #actions>
            <a @click.prevent.stop="open_playlist_dialog(playlist)">
              <mdicon
                class="icon has-text-dark"
                name="dots-vertical"
                size="16"
              />
            </a>
          </template>
        </list-item-playlist-spotify>
        <modal-dialog-playlist-spotify
          :show="show_playlist_details_modal"
          :playlist="selected_playlist"
          @close="show_playlist_details_modal = false"
        />
      </template>
      <template #footer>
        <nav class="level">
          <p class="level-item">
            <router-link
              :to="{ name: 'music-spotify-featured-playlists' }"
              class="button is-light is-small is-rounded"
              >{{ $t('page.spotify.music.show-more') }}</router-link
            >
          </p>
        </nav>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import * as types from '@/store/mutation_types'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListItemAlbumSpotify from '@/components/ListItemAlbumSpotify.vue'
import ListItemPlaylistSpotify from '@/components/ListItemPlaylistSpotify.vue'
import ModalDialogAlbumSpotify from '@/components/ModalDialogAlbumSpotify.vue'
import ModalDialogPlaylistSpotify from '@/components/ModalDialogPlaylistSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import TabsMusic from '@/components/TabsMusic.vue'
import store from '@/store'

const dataObject = {
  load(to) {
    if (
      store.state.spotify_new_releases.length > 0 &&
      store.state.spotify_featured_playlists.length > 0
    ) {
      return Promise.resolve()
    }

    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(store.state.spotify.webapi_token)
    return Promise.all([
      spotifyApi.getNewReleases({
        country: store.state.spotify.webapi_country,
        limit: 50
      }),
      spotifyApi.getFeaturedPlaylists({
        country: store.state.spotify.webapi_country,
        limit: 50
      })
    ])
  },

  set(vm, response) {
    if (response) {
      store.commit(types.SPOTIFY_NEW_RELEASES, response[0].albums.items)
      store.commit(
        types.SPOTIFY_FEATURED_PLAYLISTS,
        response[1].playlists.items
      )
    }
  }
}

export default {
  name: 'PageMusicSpotify',
  components: {
    ContentWithHeading,
    ListItemAlbumSpotify,
    ListItemPlaylistSpotify,
    ModalDialogAlbumSpotify,
    ModalDialogPlaylistSpotify,
    TabsMusic
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
      selected_album: {},
      selected_playlist: {},
      show_album_details_modal: false,
      show_playlist_details_modal: false
    }
  },

  computed: {
    featured_playlists() {
      return this.$store.state.spotify_featured_playlists.slice(0, 3)
    },
    new_releases() {
      return this.$store.state.spotify_new_releases.slice(0, 3)
    }
  },

  methods: {
    open_album_dialog(album) {
      this.selected_album = album
      this.show_album_details_modal = true
    },
    open_playlist_dialog(playlist) {
      this.selected_playlist = playlist
      this.show_playlist_details_modal = true
    }
  }
}
</script>

<style></style>
