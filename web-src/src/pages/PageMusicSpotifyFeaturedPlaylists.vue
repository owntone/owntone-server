<template>
  <div class="fd-page-with-tabs">
    <tabs-music />
    <content-with-heading>
      <template #heading-left>
        <p
          class="title is-4"
          v-text="$t('page.spotify.music.featured-playlists')"
        />
      </template>
      <template #content>
        <list-playlists-spotify :items="featured_playlists" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
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
    vm.featured_playlists = response.playlists.items
  }
}

export default {
  name: 'PageMusicSpotifyFeaturedPlaylists',
  components: {
    ContentWithHeading,
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
      featured_playlists: []
    }
  }
}
</script>

<style></style>
