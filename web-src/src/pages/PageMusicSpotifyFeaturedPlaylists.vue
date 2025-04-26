<template>
  <tabs-music />
  <content-with-heading>
    <template #heading>
      <heading-title :content="heading" />
    </template>
    <template #content>
      <list-playlists-spotify :items="playlists" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListPlaylistsSpotify from '@/components/ListPlaylistsSpotify.vue'
import SpotifyWebApi from 'spotify-web-api-js'
import TabsMusic from '@/components/TabsMusic.vue'
import webapi from '@/webapi'

export default {
  name: 'PageMusicSpotifyFeaturedPlaylists',
  components: {
    ContentWithHeading,
    HeadingTitle,
    ListPlaylistsSpotify,
    TabsMusic
  },
  beforeRouteEnter(to, from, next) {
    webapi.spotify().then(({ data }) => {
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
