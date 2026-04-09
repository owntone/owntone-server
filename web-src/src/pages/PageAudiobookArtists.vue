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

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListArtists from '@/components/ListArtists.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import library from '@/api/library'

export default {
  name: 'PageAudiobookArtists',
  components: {
    ContentWithHeading,
    ListArtists,
    ListIndexButtons,
    PaneTitle,
    TabsAudiobooks
  },
  data() {
    return { artists: new GroupedList() }
  },
  computed: {
    heading() {
      return {
        subtitle: [{ count: this.artists.count, key: 'data.authors' }],
        title: this.$t('page.audiobooks.artists.title')
      }
    }
  },
  async mounted() {
    const artists = await library.artists('audiobook')
    this.artists = new GroupedList(artists, {
      index: { field: 'name_sort', type: String }
    })
  }
}
</script>
