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

<script setup>
import { computed, onMounted, ref } from 'vue'

import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListArtistsSpotify from '@/components/ListArtistsSpotify.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import services from '@/api/services'
import { useI18n } from 'vue-i18n'

const PAGE_SIZE = 50

const { t } = useI18n()

const artists = ref([])

const heading = computed(() => ({
  title: t('page.spotify.music.followed-artists')
}))

onMounted(async () => {
  const { api } = await services.spotify.get()
  const response = await api.currentUser.followedArtists(null, PAGE_SIZE)
  artists.value = response.artists.items
})
</script>
