<template>
  <div>
    <tabs-music />
    <content-with-heading>
      <template #heading-left>
        <heading-title
          :content="{ title: $t('page.spotify.music.new-releases') }"
        />
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
      <template #heading-left>
        <heading-title
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
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import HeadingTitle from '@/components/HeadingTitle.vue'
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
    HeadingTitle,
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
