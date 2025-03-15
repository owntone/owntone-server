<template>
  <div>
    <tabs-audiobooks />
    <content-with-heading>
      <template #options>
        <index-button-list :indices="genres.indices" />
      </template>
      <template #heading>
        <heading-title :content="heading" />
      </template>
      <template #content>
        <list-genres :items="genres" :media_kind="'audiobook'" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import HeadingTitle from '@/components/HeadingTitle.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListGenres from '@/components/ListGenres.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return webapi.library_genres('audiobook')
  },
  set(vm, response) {
    vm.genres = new GroupedList(response.data.genres, {
      index: { field: 'name_sort', type: String }
    })
  }
}

export default {
  name: 'PageAudiobooksGenres',
  components: {
    ContentWithHeading,
    HeadingTitle,
    IndexButtonList,
    ListGenres,
    TabsAudiobooks
  },
  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
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
