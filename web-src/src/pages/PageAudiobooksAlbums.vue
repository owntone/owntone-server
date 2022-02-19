<template>
  <div class="fd-page-with-tabs">
    <tabs-audiobooks />

    <content-with-heading>
      <template #options>
        <index-button-list :index="albums_list.indexList" />
      </template>
      <template #heading-left>
        <p class="title is-4">Audiobooks</p>
        <p class="heading">
          {{ albums_list.sortedAndFiltered.length }} Audiobooks
        </p>
      </template>
      <template #content>
        <list-albums :albums="albums_list" />
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
import Albums from '@/lib/Albums'

const dataObject = {
  load: function (to) {
    return webapi.library_albums('audiobook')
  },

  set: function (vm, response) {
    vm.albums = response.data
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
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  },

  data() {
    return {
      albums: { items: [] }
    }
  },

  computed: {
    albums_list() {
      return new Albums(this.albums.items, {
        sort: 'Name',
        group: true
      })
    }
  },

  methods: {}
}
</script>

<style></style>
