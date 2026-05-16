<template>
  <control-tab-list :links="links" />
</template>

<script setup>
import ControlTabList from '@/components/ControlTabList.vue'
import { computed } from 'vue'
import { useServicesStore } from '@/stores/services'

const servicesStore = useServicesStore()

const links = computed(() => {
  const items = [
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
  if (servicesStore.isSpotifyActive) {
    items.push({
      icon: 'spotify',
      key: 'page.audiobooks.tabs.spotify',
      to: { name: 'audiobook-spotify' }
    })
  }
  return items
})
</script>
