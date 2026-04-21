<template>
  <tabs-music />
  <content-with-heading>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-albums-spotify :items="albums" />
    </template>
  </content-with-heading>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListAlbumsSpotify from '@/components/ListAlbumsSpotify.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import services from '@/api/services'
import { useI18n } from 'vue-i18n'

const PAGE_SIZE = 50

const albums = ref([])

const { t } = useI18n()
const heading = computed(() => ({
  title: t('page.spotify.music.new-releases')
}))

onMounted(async () => {
  const { api, configuration } = await services.spotify.get()
  const response = await api.browse.getNewReleases(
    configuration.webapi_country,
    PAGE_SIZE
  )
  albums.value = response.albums.items
})
</script>
