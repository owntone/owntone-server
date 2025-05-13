<template>
  <tabs-music />
  <content-with-heading>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-albums-spotify :items="albums" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import ListAlbumsSpotify from '@/components/ListAlbumsSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import TabsMusic from '@/components/TabsMusic.vue'
import services from '@/api/services'

export default {
  name: 'PageMusicSpotifyNewReleases',
  components: {
    ContentWithHeading,
    PaneTitle,
    ListAlbumsSpotify,
    TabsMusic
  },
  beforeRouteEnter(to, from, next) {
    services.spotify().then((data) => {
      const spotifyApi = new SpotifyWebApi()
      spotifyApi.setAccessToken(data.webapi_token)
      spotifyApi
        .getNewReleases({
          country: data.webapi_country,
          limit: 50
        })
        .then((response) => {
          next((vm) => {
            vm.albums = response.albums.items
          })
        })
    })
  },
  data() {
    return {
      albums: []
    }
  },
  computed: {
    heading() {
      return { title: this.$t('page.spotify.music.new-releases') }
    }
  }
}
</script>
