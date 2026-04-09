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

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import library from '@/api/library'

export default {
  name: 'PageAudiobookAlbums',
  components: {
    ContentWithHeading,
    ListAlbums,
    ListIndexButtons,
    PaneTitle,
    TabsAudiobooks
  },
  data() {
    return { albums: new GroupedList() }
  },
  computed: {
    heading() {
      return {
        subtitle: [{ count: this.albums.count, key: 'data.audiobooks' }],
        title: this.$t('page.audiobooks.albums.title')
      }
    }
  },
  async mounted() {
    const albums = await library.albums('audiobook')
    this.albums = new GroupedList(albums, {
      index: { field: 'name_sort', type: String }
    })
  }
}
</script>
