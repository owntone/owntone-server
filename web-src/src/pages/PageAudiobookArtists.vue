<template>
  <tabs-audiobooks />
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="artists.indices" />
    </template>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-artists :items="artists" />
    </template>
  </content-with-heading>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListArtists from '@/components/ListArtists.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import library from '@/api/library'
import { useI18n } from 'vue-i18n'

const { t } = useI18n()

const artists = ref(new GroupedList())

const heading = computed(() => ({
  subtitle: [{ count: artists.value.count, key: 'data.authors' }],
  title: t('page.audiobooks.artists.title')
}))

onMounted(async () => {
  const artistsData = await library.artists('audiobook')
  artists.value = new GroupedList(artistsData, {
    index: { field: 'name_sort', type: String }
  })
})
</script>
