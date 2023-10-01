<template>
  <div class="fd-page-with-tabs">
    <tabs-music />
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
    </content-with-heading>
  </div>
</template>

<script>
import * as types from '@/store/mutation_types'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import CoverArtwork from '@/components/CoverArtwork.vue'
import ListItemAlbumSpotify from '@/components/ListItemAlbumSpotify.vue'
import ModalDialogAlbumSpotify from '@/components/ModalDialogAlbumSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import store from '@/store'
import TabsMusic from '@/components/TabsMusic.vue'

const dataObject = {
  load(to) {
    if (store.state.spotify_new_releases.length > 0) {
      return Promise.resolve()
    }

    const spotifyApi = new SpotifyWebApi()
    spotifyApi.setAccessToken(store.state.spotify.webapi_token)
    return spotifyApi.getNewReleases({
      country: store.state.spotify.webapi_country,
      limit: 50
    })
  },

  set(vm, response) {
    if (response) {
      store.commit(types.SPOTIFY_NEW_RELEASES, response.albums.items)
    }
  }
}

export default {
  name: 'PageBrowseSpotifyNewReleases',
  components: {
    ContentWithHeading,
    CoverArtwork,
    ListItemAlbumSpotify,
    ModalDialogAlbumSpotify,
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
      selected_album: {}
    }
  },

  computed: {
    new_releases() {
      return this.$store.state.spotify_new_releases
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
