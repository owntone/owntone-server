<template>
  <div class="fd-page-with-tabs">
    <tabs-audiobooks />

    <content-with-heading>
      <template #options>
        <index-button-list :index="albums.indexList" />
      </template>
      <template #heading-left>
        <p class="title is-4">Audiobooks</p>
        <p class="heading">{{ albums.count }} Audiobooks</p>
      </template>
      <template #content>
        <list-albums :albums="albums" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import webapi from '@/webapi'
import { bySortName, GroupByList } from '@/lib/GroupByList'

const dataObject = {
  load: function (to) {
    return webapi.library_albums('audiobook')
  },

  set: function (vm, response) {
    vm.albums = new GroupByList(response.data)
    vm.albums.group(bySortName('name_sort'))
  }
}

export default {
  name: 'PageAudiobooksAlbums',
  components: {
    TabsAudiobooks,
    ContentWithHeading,
    IndexButtonList,
    ListAlbums
  },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },

  beforeRouteUpdate(to, from, next) {
    if (!this.albums.isEmpty()) {
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
      albums: new GroupByList()
    }
  },

  methods: {}
}
</script>

<style></style>
