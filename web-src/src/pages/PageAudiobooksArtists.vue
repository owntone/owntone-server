<template>
  <div class="fd-page-with-tabs">
    <tabs-audiobooks />

    <content-with-heading>
      <template #options>
        <index-button-list :index="artists.indexList" />
      </template>
      <template #heading-left>
        <p class="title is-4">Authors</p>
        <p class="heading">{{ artists.count }} Authors</p>
      </template>
      <template #heading-right />
      <template #content>
        <list-artists :artists="artists" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListArtists from '@/components/ListArtists.vue'
import webapi from '@/webapi'
import { bySortName, GroupByList } from '@/lib/GroupByList'

const dataObject = {
  load: function (to) {
    return webapi.library_artists('audiobook')
  },

  set: function (vm, response) {
    vm.artists_list = new GroupByList(response.data)
  }
}

export default {
  name: 'PageAudiobooksArtists',
  components: {
    ContentWithHeading,
    TabsAudiobooks,
    IndexButtonList,
    ListArtists
  },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },

  beforeRouteUpdate(to, from, next) {
    if (!this.artists_list.isEmpty()) {
      next()
      return
    }
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  },

  data() {
    return {
      artists_list: new GroupByList()
    }
  },

  computed: {
    artists() {
      if (!this.artists_list) {
        return []
      }
      this.artists_list.group(bySortName('name_sort'))
      return this.artists_list
    }
  },

  methods: {}
}
</script>

<style></style>
