<template>
  <tabs-music />
  <content-with-heading>
    <template #heading>
      <pane-title
        :content="{ title: $t('page.music.recently-played.title') }"
      />
    </template>
    <template #content>
      <list-tracks :items="tracks" />
    </template>
  </content-with-heading>
</template>

<script setup>
import { onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListTracks from '@/components/ListTracks.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import library from '@/api/library'

const tracks = ref(new GroupedList())

onMounted(async () => {
  const data = await library.search({
    expression:
      'time_played after 8 weeks ago and media_kind is music order by time_played desc',
    limit: 50,
    type: 'track'
  })
  tracks.value = new GroupedList(data.tracks)
})
</script>
