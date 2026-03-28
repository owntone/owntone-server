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
import ListPlaylistsSpotify from '@/components/ListPlaylistsSpotify.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import services from '@/api/services'

export default {
  name: 'PageMusicSpotifyFeaturedPlaylists',
  components: {
    ContentWithHeading,
    ListPlaylistsSpotify,
    PaneTitle,
    TabsMusic
  },
  data() {
    return { playlists: [] }
  },
  computed: {
    heading() {
      return { title: this.$t('page.spotify.music.featured-playlists') }
    }
  },
  async mounted() {
    const { api, configuration } = await services.spotify()
    const response = await api.browse.getFeaturedPlaylists(
      configuration.webapi_country,
      null,
      null,
      50
    )
    this.playlists = response.playlists.items
  }
}
</script>
