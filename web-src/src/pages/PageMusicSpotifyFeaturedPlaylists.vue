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

const dataObject = {
  load(to) {
    return webapi.spotify().then(({ data }) => {
      const spotifyApi = new SpotifyWebApi()
      spotifyApi.setAccessToken(data.webapi_token)
      return spotifyApi.getFeaturedPlaylists({
        country: data.webapi_country,
        limit: 50
      })
    })
  },
  set(vm, response) {
    vm.playlists = response.playlists.items
  }
}

export default {
  name: 'PageMusicSpotifyFeaturedPlaylists',
  components: {
    ContentWithHeading,
    HeadingTitle,
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
