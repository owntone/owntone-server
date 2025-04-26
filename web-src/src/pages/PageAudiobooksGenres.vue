<template>
  <tabs-audiobooks />
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="genres.indices" />
    </template>
    <template #heading>
      <heading-title :content="heading" />
    </template>
    <template #content>
      <list-genres :items="genres" media-kind="audiobook" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListGenres from '@/components/ListGenres.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import webapi from '@/webapi'

export default {
  name: 'PageAudiobooksGenres',
  components: {
    ContentWithHeading,
    HeadingTitle,
    ListIndexButtons,
    ListGenres,
    TabsAudiobooks
  },
  beforeRouteEnter(to, from, next) {
    webapi.library_genres('audiobook').then((response) => {
      next((vm) => {
        vm.genres = new GroupedList(response.data.genres, {
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
        subtitle: [{ count: this.genres.total, key: 'count.genres' }],
        title: this.$t('page.genres.title')
      }
    }
  }
}
</script>
