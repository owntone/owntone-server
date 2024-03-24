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
        </list-item-album-spotify>
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
        </list-item-playlist-spotify>
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

  computed: {
    featured_playlists() {
      return this.$store.state.spotify_featured_playlists.slice(0, 3)
    },
    new_releases() {
      return this.$store.state.spotify_new_releases.slice(0, 3)
    }
  }
}
</script>

<style></style>
