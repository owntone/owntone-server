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
          to: { name: 'music-history' },
          icon: 'history',
          label: 'page.music.tabs.history'
        },
        {
          to: { name: 'music-artists' },
          icon: 'account-music',
          label: 'page.music.tabs.artists'
        },
        {
          to: { name: 'music-albums' },
          icon: 'album',
          label: 'page.music.tabs.albums'
        },
        {
          to: { name: 'music-genres' },
          icon: 'speaker',
          label: 'page.music.tabs.genres'
        },
        {
          to: { name: 'music-composers' },
          icon: 'book-open-page-variant',
          label: 'page.music.tabs.composers'
        }
      ]
      if (this.servicesStore.spotify.webapi_token_valid) {
        links.push({
          to: { name: 'music-spotify' },
          icon: 'spotify',
          label: 'page.music.tabs.spotify'
        })
      }
      return links
    }
  }
}
</script>
