<template>
  <tabs-music />
  <content-with-heading>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-artists-spotify :items="artists" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListArtistsSpotify from '@/components/ListArtistsSpotify.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import services from '@/api/services'

export default {
  name: 'PageMusicSpotifyFeaturedPlaylists',
  components: {
    ContentWithHeading,
    ListArtistsSpotify,
    PaneTitle,
    TabsMusic
  },
  beforeRouteEnter(to, from, next) {
    services.spotify().then(({ api }) => {
      api.currentUser.followedArtists(null, 50).then((response) => {
        next((vm) => {
          vm.artists = response.artists.items
        })
      })
    })
  },
  data() {
    return { artists: [] }
  },
  computed: {
    heading() {
      return { title: this.$t('page.spotify.music.followed-artists') }
    }
  }
}
</script>
