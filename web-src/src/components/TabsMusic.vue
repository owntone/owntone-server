<template>
  <control-tab-list :links="links" />
</template>

<script>
import ControlTabList from '@/components/ControlTabList.vue'
import { useServicesStore } from '@/stores/services'

export default {
  name: 'TabsMusic',
  components: { ControlTabList },
  setup() {
    return { servicesStore: useServicesStore() }
  },
  computed: {
    links() {
      const links = [
        {
          icon: 'history',
          key: 'page.music.tabs.history',
          to: { name: 'music-history' }
        },
        {
          icon: 'account-music',
          key: 'page.music.tabs.artists',
          to: { name: 'music-artists' }
        },
        {
          icon: 'album',
          key: 'page.music.tabs.albums',
          to: { name: 'music-albums' }
        },
        {
          icon: 'speaker',
          key: 'page.music.tabs.genres',
          to: { name: 'music-genres' }
        },
        {
          icon: 'book-open-page-variant',
          key: 'page.music.tabs.composers',
          to: { name: 'music-composers' }
        }
      ]
      if (this.servicesStore.isSpotifyActive) {
        links.push({
          icon: 'spotify',
          key: 'page.music.tabs.spotify',
          to: { name: 'music-spotify' }
        })
      }
      return links
    }
  }
}
</script>
