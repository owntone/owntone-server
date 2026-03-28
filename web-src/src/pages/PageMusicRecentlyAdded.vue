<template>
  <tabs-music />
  <content-with-heading>
    <template #heading>
      <pane-title :content="{ title: $t('page.music.recently-added.title') }" />
    </template>
    <template #content>
      <list-albums :items="albums" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import library from '@/api/library'
import { useSettingsStore } from '@/stores/settings'

export default {
  name: 'PageMusicRecentlyAdded',
  components: { ContentWithHeading, ListAlbums, PaneTitle, TabsMusic },
  setup() {
    return {
      settingsStore: useSettingsStore()
    }
  },
  data() {
    return {
      albums: new GroupedList()
    }
  },
  async mounted() {
    const limit = useSettingsStore().recentlyAddedLimit
    const data = await library.search({
      expression:
        'media_kind is music having track_count > 3 order by time_added desc',
      limit,
      type: 'album'
    })
    this.albums = new GroupedList(data.albums, {
      criteria: [{ field: 'time_added', order: -1, type: Date }],
      index: { field: 'time_added', type: Date }
    })
  }
}
</script>
