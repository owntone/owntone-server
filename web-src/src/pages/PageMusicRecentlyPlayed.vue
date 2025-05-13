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

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import PaneTitle from '@/components/PaneTitle.vue'
import ListTracks from '@/components/ListTracks.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import library from '@/api/library'

export default {
  name: 'PageMusicRecentlyPlayed',
  components: { ContentWithHeading, PaneTitle, ListTracks, TabsMusic },
  beforeRouteEnter(to, from, next) {
    library
      .search({
        expression:
          'time_played after 8 weeks ago and media_kind is music order by time_played desc',
        limit: 50,
        type: 'track'
      })
      .then((data) => {
        next((vm) => {
          vm.tracks = new GroupedList(data.tracks)
        })
      })
  },
  data() {
    return {
      tracks: new GroupedList()
    }
  }
}
</script>
