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
          label: 'page.audiobooks.tabs.authors'
        },
        {
          to: { name: 'audiobooks-albums' },
          icon: 'album',
          label: 'page.audiobooks.tabs.audiobooks'
        },
        {
          to: { name: 'audiobooks-genres' },
          icon: 'speaker',
          label: 'page.audiobooks.tabs.genres'
        }
      ]
    }
  }
}
</script>
