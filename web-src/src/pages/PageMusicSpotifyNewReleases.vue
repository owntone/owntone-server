<template>
  <div class="fd-page-with-tabs">
    <tabs-music />
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
          :show="show_details_modal"
          :album="selected_album"
          @close="show_details_modal = false"
        />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import * as types from '@/store/mutation_types'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListItemAlbumSpotify from '@/components/ListItemAlbumSpotify.vue'
import ModalDialogAlbumSpotify from '@/components/ModalDialogAlbumSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import TabsMusic from '@/components/TabsMusic.vue'
import store from '@/store'

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
  name: 'PageMusicSpotifyNewReleases',
  components: {
    ContentWithHeading,
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
      selected_album: {},
      show_details_modal: false
    }
  },

  computed: {
    new_releases() {
      return this.$store.state.spotify_new_releases
    }
  },

  methods: {
    open_album_dialog(album) {
      this.selected_album = album
      this.show_details_modal = true
    }
  }
}
</script>

<style></style>
