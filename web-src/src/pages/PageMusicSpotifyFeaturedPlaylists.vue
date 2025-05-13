<template>
  <tabs-music />
  <content-with-heading>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-playlists-spotify :items="playlists" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import ListPlaylistsSpotify from '@/components/ListPlaylistsSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import TabsMusic from '@/components/TabsMusic.vue'
import services from '@/api/services'

export default {
  name: 'PageMusicSpotifyFeaturedPlaylists',
  components: {
    ContentWithHeading,
    PaneTitle,
    ListPlaylistsSpotify,
    TabsMusic
  },
  beforeRouteEnter(to, from, next) {
    services.spotify().then((data) => {
      const spotifyApi = new SpotifyWebApi()
      spotifyApi.setAccessToken(data.webapi_token)
      spotifyApi
        .getFeaturedPlaylists({
          country: data.webapi_country,
          limit: 50
        })
        .then((response) => {
          next((vm) => {
            vm.playlists = response.playlists.items
          })
        })
    })
  },
  data() {
    return {
      playlists: []
    }
  },
  computed: {
    heading() {
      return { title: this.$t('page.spotify.music.featured-playlists') }
    }
  }
}
</script>
