<template>
  <div>
    <tabs-music />
    <content-with-heading>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.spotify.music.new-releases')" />
      </template>
      <template #content>
        <list-albums-spotify :items="albums" />
      </template>
      <template #footer>
        <nav class="level">
          <p class="level-item">
            <router-link
              :to="{ name: 'music-spotify-new-releases' }"
              class="button is-light is-small is-rounded"
            >
              {{ $t('page.spotify.music.show-more') }}
            </router-link>
          </p>
        </nav>
      </template>
    </content-with-heading>
    <content-with-heading>
      <template #heading-left>
        <p
          class="title is-4"
          v-text="$t('page.spotify.music.featured-playlists')"
        />
      </template>
      <template #content>
        <list-playlists-spotify :items="playlists" />
      </template>
      <template #footer>
        <nav class="level">
          <p class="level-item">
            <router-link
              :to="{ name: 'music-spotify-featured-playlists' }"
              class="button is-light is-small is-rounded"
            >
              {{ $t('page.spotify.music.show-more') }}
            </router-link>
          </p>
        </nav>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListAlbumsSpotify from '@/components/ListAlbumsSpotify.vue'
import ListPlaylistsSpotify from '@/components/ListPlaylistsSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import TabsMusic from '@/components/TabsMusic.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return webapi.spotify().then(({ data }) => {
      const spotifyApi = new SpotifyWebApi()
      spotifyApi.setAccessToken(data.webapi_token)
      return Promise.all([
        spotifyApi.getNewReleases({
          country: data.webapi_country,
          limit: 3
        }),
        spotifyApi.getFeaturedPlaylists({
          country: data.webapi_country,
          limit: 3
        })
      ])
    })
  },

  set(vm, response) {
    vm.albums = response[0].albums.items
    vm.playlists = response[1].playlists.items
  }
}

export default {
  name: 'PageMusicSpotify',
  components: {
    ContentWithHeading,
    ListAlbumsSpotify,
    ListPlaylistsSpotify,
    TabsMusic
  },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },

  data() {
    return {
      playlists: [],
      albums: []
    }
  }
}
</script>
