<template>
  <div class="fd-page-with-tabs">
    <tabs-music />
    <content-with-heading>
      <template #options>
        <index-button-list :index="genres.indexList" />
      </template>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.genres.title')" />
        <p
          class="heading"
          v-text="$t('page.genres.count', { count: genres.total })"
        />
      </template>
      <template #content>
        <list-genres :genres="genres" :media_kind="'music'" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { GroupByList, byName } from '@/lib/GroupByList'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListGenres from '@/components/ListGenres.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return webapi.library_genres('music')
  },

  set(vm, response) {
    vm.genres = response.data
    vm.genres = new GroupByList(response.data)
    vm.genres.group(byName('name_sort'))
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
  beforeRouteUpdate(to, from, next) {
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  },

  data() {
    return {
      genres: new GroupByList()
    }
  }
}
</script>

<style></style>
