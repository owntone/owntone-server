<template>
  <tabs-audiobooks />
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="genres.indices" />
    </template>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-genres :items="genres" media-kind="audiobook" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListGenres from '@/components/ListGenres.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import library from '@/api/library'

export default {
  name: 'PageAudiobooksGenres',
  components: {
    ContentWithHeading,
    ListGenres,
    ListIndexButtons,
    PaneTitle,
    TabsAudiobooks
  },
  data() {
    return {
      genres: new GroupedList()
    }
  },
  computed: {
    heading() {
      return {
        subtitle: [{ count: this.genres.total, key: 'data.genres' }],
        title: this.$t('page.genres.title')
      }
    }
  },
  async mounted() {
    const genres = await library.genres('audiobook')
    this.genres = new GroupedList(genres, {
      index: { field: 'name_sort', type: String }
    })
  }
}
</script>
