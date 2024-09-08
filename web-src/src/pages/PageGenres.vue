<template>
  <div>
    <tabs-music />
    <content-with-heading>
      <template #options>
        <index-button-list :indices="genres.indices" />
      </template>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.genres.title')" />
        <p
          class="heading"
          v-text="$t('page.genres.count', { count: genres.total })"
        />
      </template>
      <template #content>
        <list-genres :items="genres" :media_kind="'music'" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListGenres from '@/components/ListGenres.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
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
    IndexButtonList,
    ListGenres,
    TabsMusic
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
  }
}
</script>
