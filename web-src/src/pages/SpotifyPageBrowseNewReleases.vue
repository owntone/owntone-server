<template>
  <div class="fd-page-with-tabs">
    <tabs-music />

    <content-with-heading>
      <template #heading-left>
        <p class="title is-4">New Releases</p>
      </template>
      <template #content>
        <spotify-list-item-album
          v-for="album in new_releases"
          :key="album.id"
          :album="album"
          @click="open_album(album)"
        >
          <template v-if="is_visible_artwork" #artwork>
            <p class="image is-64x64 fd-has-shadow fd-has-action">
              <cover-artwork
                :artwork_url="artwork_url(album)"
                :artist="album.artist"
                :album="album.name"
                :maxwidth="64"
                :maxheight="64"
              />
            </p>
          </template>
          <template #actions>
            <a @click="open_album_dialog(album)">
              <span class="icon has-text-dark"
                ><i class="mdi mdi-dots-vertical mdi-18px"
              /></span>
            </a>
          </template>
        </spotify-list-item-album>
        <spotify-modal-dialog-album
          :show="show_album_details_modal"
          :album="selected_album"
          @close="show_album_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import SpotifyListItemAlbum from '@/components/SpotifyListItemAlbum.vue'
import SpotifyModalDialogAlbum from '@/components/SpotifyModalDialogAlbum.vue'
import CoverArtwork from '@/components/CoverArtwork.vue'
import store from '@/store'
import * as types from '@/store/mutation_types'
import SpotifyWebApi from 'spotify-web-api-js'

const dataObject = {
  load: function (to) {
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

  set: function (vm, response) {
    if (response) {
      store.commit(types.SPOTIFY_NEW_RELEASES, response.albums.items)
    }
  }
}

export default {
  name: 'SpotifyPageBrowseNewReleases',
  components: {
    ContentWithHeading,
    TabsMusic,
    SpotifyListItemAlbum,
    SpotifyModalDialogAlbum,
    CoverArtwork
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
    open_album: function (album) {
      this.$router.push({ path: '/music/spotify/albums/' + album.id })
    },

    open_album_dialog: function (album) {
      this.selected_album = album
      this.show_album_details_modal = true
    },

    artwork_url: function (album) {
      if (album.images && album.images.length > 0) {
        return album.images[0].url
      }
      return ''
    }
  }
}
</script>

<style></style>
