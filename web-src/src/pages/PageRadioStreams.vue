<template>
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="tracks.indices" />
    </template>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-tracks :items="tracks" />
    </template>
  </content-with-heading>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import ListTracks from '@/components/ListTracks.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import library from '@/api/library'
import { useI18n } from 'vue-i18n'

const { t } = useI18n()

const tracks = ref(new GroupedList())

const heading = computed(() => ({
  subtitle: [{ count: tracks.value.total, key: 'data.stations' }],
  title: t('page.radio.title')
}))

onMounted(async () => {
  const data = await library.radioStreams()
  tracks.value = new GroupedList(data, {
    index: { field: 'title_sort', type: String }
  })
})
</script>
