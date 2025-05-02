<template>
  <tabs-music />
  <content-with-heading>
    <template #heading>
      <heading-title
        :content="{ title: $t('page.music.recently-added.title') }"
      />
    </template>
    <template #content>
      <list-albums :items="albums" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import { useSettingsStore } from '@/stores/settings'
import webapi from '@/webapi'

export default {
  name: 'PageMusicRecentlyAdded',
  components: { ContentWithHeading, HeadingTitle, ListAlbums, TabsMusic },
  beforeRouteEnter(to, from, next) {
    const limit = useSettingsStore().recentlyAddedLimit
    webapi
      .search({
        expression:
          'media_kind is music having track_count > 3 order by time_added desc',
        limit,
        type: 'album'
      })
      .then((data) => {
        next((vm) => {
          vm.albums = new GroupedList(data.albums, {
            criteria: [{ field: 'time_added', order: -1, type: Date }],
            index: { field: 'time_added', type: Date }
          })
        })
      })
  },
  setup() {
    return {
      settingsStore: useSettingsStore()
    }
  },
  data() {
    return {
      albums: new GroupedList()
    }
  }
}
</script>
