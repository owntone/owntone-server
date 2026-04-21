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

<script setup>
import { computed, onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListPlaylistsSpotify from '@/components/ListPlaylistsSpotify.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import services from '@/api/services'
import { useI18n } from 'vue-i18n'

const PAGE_SIZE = 50

const { t } = useI18n()

const playlists = ref([])

const heading = computed(() => ({
  title: t('page.spotify.music.featured-playlists')
}))

onMounted(async () => {
  const { api, configuration } = await services.spotify.get()
  const response = await api.browse.getFeaturedPlaylists(
    configuration.webapi_country,
    null,
    null,
    PAGE_SIZE
  )
  playlists.value = response.playlists.items
})
</script>
