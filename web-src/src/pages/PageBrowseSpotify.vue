<template>
  <div class="fd-page-with-tabs">
    <tabs-music />
    <!-- New Releases -->
    <content-with-heading>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.spotify.browse.new-releases')" />
      </template>
      <template #content>
        <list-item-album-spotify
          v-for="album in new_releases"
          :key="album.id"
          :album="album"
          @click="open_album(album)"
        >
          <template v-if="is_visible_artwork" #artwork>
            <cover-artwork
              :artwork_url="artwork_url(album)"
              :artist="album.artist"
              :album="album.name"
              class="is-clickable fd-has-shadow fd-cover fd-cover-small-image"
              :maxwidth="64"
              :maxheight="64"
            />
          </template>
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
              >{{ $t('page.spotify.browse.show-more') }}</router-link
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
          v-text="$t('page.spotify.browse.featured-playlists')"
        />
      </template>
      <template #content>
        <list-item-playlist-spotify
          v-for="playlist in featured_playlists"
          :key="playlist.id"
          :playlist="playlist"
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
              >{{ $t('page.spotify.browse.show-more') }}</router-link
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
import CoverArtwork from '@/components/CoverArtwork.vue'
import ListItemAlbumSpotify from '@/components/ListItemAlbumSpotify.vue'
import ListItemPlaylistSpotify from '@/components/ListItemPlaylistSpotify.vue'
import ModalDialogAlbumSpotify from '@/components/ModalDialogAlbumSpotify.vue'
import ModalDialogPlaylistSpotify from '@/components/ModalDialogPlaylistSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import store from '@/store'
import TabsMusic from '@/components/TabsMusic.vue'

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
  name: 'SpotifyPageBrowse',
  components: {
    ContentWithHeading,
    CoverArtwork,
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
      show_album_details_modal: false,
      selected_album: {},

      show_playlist_details_modal: false,
      selected_playlist: {}
    }
  },

  computed: {
    new_releases() {
      return this.$store.state.spotify_new_releases.slice(0, 3)
    },

    featured_playlists() {
      return this.$store.state.spotify_featured_playlists.slice(0, 3)
    },

    is_visible_artwork() {
      return this.$store.getters.settings_option(
        'webinterface',
        'show_cover_artwork_in_album_lists'
      ).value
    }
  },

  methods: {
    open_album(album) {
      this.$router.push({
        name: 'music-spotify-album',
        params: { id: album.id }
      })
    },

    open_album_dialog(album) {
      this.selected_album = album
      this.show_album_details_modal = true
    },

    open_playlist_dialog(playlist) {
      this.selected_playlist = playlist
      this.show_playlist_details_modal = true
    },

    artwork_url(album) {
      if (album.images && album.images.length > 0) {
        return album.images[0].url
      }
      return ''
    }
  }
}
</script>

<style></style>
