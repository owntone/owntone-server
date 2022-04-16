<template>
  <div class="fd-page-with-tabs">
    <tabs-music />

    <content-with-heading>
      <template #options>
        <index-button-list :index="genres.indexList" />
      </template>
      <template #heading-left>
        <p class="title is-4">Genres</p>
        <p class="heading">{{ genres.total }} genres</p>
      </template>
      <template #content>
        <list-genres :genres="genres" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListGenres from '@/components/ListGenres.vue'
import webapi from '@/webapi'
import { byName, GroupByList } from '@/lib/GroupByList'

const dataObject = {
  load: function (to) {
    return webapi.library_genres('music')
  },

  set: function (vm, response) {
    vm.genres = response.data
    vm.genres = new GroupByList(response.data)
    vm.genres.group(byName('name_sort'))
  }
}

export default {
  name: 'PageGenres',
  components: {
    ContentWithHeading,
    TabsMusic,
    IndexButtonList,
    ListGenres
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
  },

  computed: {},

  methods: {}
}
</script>

<style></style>
