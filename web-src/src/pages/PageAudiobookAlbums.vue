<template>
  <tabs-audiobooks />
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="albums.indices" />
    </template>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-albums :items="albums" />
    </template>
  </content-with-heading>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import library from '@/api/library'
import { useI18n } from 'vue-i18n'

const { t } = useI18n()

const albums = ref(new GroupedList())

const heading = computed(() => ({
  subtitle: [{ count: albums.value.count, key: 'data.audiobooks' }],
  title: t('page.audiobooks.albums.title')
}))

onMounted(async () => {
  const data = await library.albums('audiobook')
  albums.value = new GroupedList(data, {
    index: { field: 'name_sort', type: String }
  })
})
</script>
