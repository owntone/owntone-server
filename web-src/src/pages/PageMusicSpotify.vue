<template>
  <tabs-music />
  <content-with-heading>
    <template #heading>
      <pane-title :content="{ title: $t('page.spotify.music.new-releases') }" />
    </template>
    <template #content>
      <list-albums-spotify :items="albums" />
    </template>
    <template #footer>
      <router-link
        :to="{ name: 'music-spotify-new-releases' }"
        class="button is-small is-rounded"
      >
        {{ $t('actions.show-more') }}
      </router-link>
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <pane-title
        :content="{ title: $t('page.spotify.music.featured-playlists') }"
      />
    </template>
    <template #content>
      <list-playlists-spotify :items="playlists" />
    </template>
    <template #footer>
      <router-link
        :to="{ name: 'music-spotify-featured-playlists' }"
        class="button is-small is-rounded"
      >
        {{ $t('actions.show-more') }}
      </router-link>
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import ListAlbumsSpotify from '@/components/ListAlbumsSpotify.vue'
import ListPlaylistsSpotify from '@/components/ListPlaylistsSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import TabsMusic from '@/components/TabsMusic.vue'
import services from '@/api/services'

export default {
  name: 'PageMusicSpotify',
  components: {
    ContentWithHeading,
    PaneTitle,
    ListAlbumsSpotify,
    ListPlaylistsSpotify,
    TabsMusic
  },
  beforeRouteEnter(to, from, next) {
    services.spotify().then((data) => {
      const spotifyApi = new SpotifyWebApi()
      spotifyApi.setAccessToken(data.webapi_token)
      Promise.all([
        spotifyApi.getNewReleases({
          country: data.webapi_country,
          limit: 3
        }),
        spotifyApi.getFeaturedPlaylists({
          country: data.webapi_country,
          limit: 3
        })
      ]).then((response) => {
        next((vm) => {
          vm.albums = response[0].albums.items
          vm.playlists = response[1].playlists.items
        })
      })
    })
  },
  data() {
    return {
      albums: [],
      playlists: []
    }
  }
}
</script>
