<template>
  <tabs-music />
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="genres.indices" />
    </template>
    <template #heading>
      <heading-title :content="heading" />
    </template>
    <template #content>
      <list-genres :items="genres" media-kind="music" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import ListGenres from '@/components/ListGenres.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import webapi from '@/webapi'

const dataObject = {
  load() {
    return webapi.library_genres('music')
  },
  set(vm, response) {
    vm.genres = new GroupedList(response.data.genres, {
      index: { field: 'name_sort', type: String }
    })
  }
}

export default {
  name: 'PageGenres',
  components: {
    ContentWithHeading,
    HeadingTitle,
    ListIndexButtons,
    ListGenres,
    TabsMusic
  },
  beforeRouteEnter(to, from, next) {
    dataObject.load().then((response) => {
      next((vm) => dataObject.set(vm, response))
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
