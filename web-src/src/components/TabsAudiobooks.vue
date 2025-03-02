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
          to: { name: 'audiobooks-artists' },
          icon: 'account-music',
          key: 'page.audiobooks.tabs.authors'
        },
        {
          to: { name: 'audiobooks-albums' },
          icon: 'album',
          key: 'page.audiobooks.tabs.audiobooks'
        },
        {
          to: { name: 'audiobooks-genres' },
          icon: 'speaker',
          key: 'page.audiobooks.tabs.genres'
        }
      ]
    }
  }
}
</script>
