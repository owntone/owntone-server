<template>
  <tabs-music />
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="genres.indices" />
    </template>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-genres :items="genres" media-kind="music" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListGenres from '@/components/ListGenres.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import library from '@/api/library'

export default {
  name: 'PageGenres',
  components: {
    ContentWithHeading,
    ListIndexButtons,
    ListGenres,
    PaneTitle,
    TabsMusic
  },
  beforeRouteEnter(to, from, next) {
    library.genres('music').then((genres) => {
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
