<template>
  <tabs-music />
  <content-with-heading>
    <template #heading>
      <heading-title
        :content="{ title: $t('page.music.recently-played.title') }"
      />
    </template>
    <template #content>
      <list-tracks :items="tracks" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListTracks from '@/components/ListTracks.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import webapi from '@/webapi'

const dataObject = {
  load() {
    return webapi.search({
      expression:
        'time_played after 8 weeks ago and media_kind is music order by time_played desc',
      limit: 50,
      type: 'track'
    })
  },
  set(vm, response) {
    vm.tracks = new GroupedList(response.data.tracks)
  }
}

export default {
  name: 'PageMusicRecentlyPlayed',
  components: { ContentWithHeading, HeadingTitle, ListTracks, TabsMusic },
  beforeRouteEnter(to, from, next) {
    dataObject.load().then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },
  data() {
    return {
      tracks: {}
    }
  }
}
</script>
