<template>
  <tabs-music />
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="genres.indices" />
    </template>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-genres :items="genres" media-kind="music" />
    </template>
  </content-with-heading>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListGenres from '@/components/ListGenres.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import library from '@/api/library'
import { useI18n } from 'vue-i18n'

const { t } = useI18n()

const genres = ref(new GroupedList())

const heading = computed(() => ({
  subtitle: [{ count: genres.value.total, key: 'data.genres' }],
  title: t('page.genres.title')
}))

onMounted(async () => {
  const genresData = await library.genres('music')
  genres.value = new GroupedList(genresData, {
    index: { field: 'name_sort', type: String }
  })
})
</script>
