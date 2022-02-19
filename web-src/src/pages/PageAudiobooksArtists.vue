<template>
  <div class="fd-page-with-tabs">
    <tabs-audiobooks />

    <content-with-heading>
      <template #options>
        <index-button-list :index="artists_list.indexList" />
      </template>
      <template #heading-left>
        <p class="title is-4">Authors</p>
        <p class="heading">
          {{ artists_list.sortedAndFiltered.length }} Authors
        </p>
      </template>
      <template #heading-right />
      <template #content>
        <list-artists :artists="artists_list" />
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
import Artists from '@/lib/Artists'

const dataObject = {
  load: function (to) {
    return webapi.library_artists('audiobook')
  },

  set: function (vm, response) {
    vm.artists = response.data
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
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  },

  data() {
    return {
      artists: { items: [] }
    }
  },

  computed: {
    artists_list() {
      return new Artists(this.artists.items, {
        sort: 'Name',
        group: true
      })
    }
  },

  methods: {}
}
</script>

<style></style>
