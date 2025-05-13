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
import PaneTitle from '@/components/PaneTitle.vue'
import ListGenres from '@/components/ListGenres.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import library from '@/api/library'

export default {
  name: 'PageAudiobooksGenres',
  components: {
    ContentWithHeading,
    PaneTitle,
    ListIndexButtons,
    ListGenres,
    TabsAudiobooks
  },
  beforeRouteEnter(to, from, next) {
    library.genres('audiobook').then((genres) => {
      next((vm) => {
        vm.genres = new GroupedList(genres, {
          index: { field: 'name_sort', type: String }
        })
      })
    })
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
  }
}
</script>
