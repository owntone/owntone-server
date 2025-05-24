<template>
  <control-tab-list :links="links" />
</template>

<script>
import ControlTabList from '@/components/ControlTabList.vue'

export default {
  name: 'TabsAudiobooks',
  components: { ControlTabList },
  computed: {
    links() {
      return [
        {
          icon: 'account-music',
          key: 'page.audiobooks.tabs.authors',
          to: { name: 'audiobook-artists' }
        },
        {
          icon: 'album',
          key: 'page.audiobooks.tabs.audiobooks',
          to: { name: 'audiobook-albums' }
        },
        {
          icon: 'speaker',
          key: 'page.audiobooks.tabs.genres',
          to: { name: 'audiobook-genres' }
        }
      ]
    }
  }
}
</script>
