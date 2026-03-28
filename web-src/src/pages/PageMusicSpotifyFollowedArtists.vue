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
  data() {
    return { artists: [] }
  },
  computed: {
    heading() {
      return { title: this.$t('page.spotify.music.followed-artists') }
    }
  },
  async mounted() {
    const { api } = await services.spotify()
    const response = await api.currentUser.followedArtists(null, 50)
    this.artists = response.artists.items
  }
}
</script>
