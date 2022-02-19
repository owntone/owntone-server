<template>
  <div class="fd-page-with-tabs">
    <tabs-music />

    <content-with-heading>
      <template #heading-left>
        <p class="title is-4">Recently added</p>
        <p class="heading">albums</p>
      </template>
      <template #content>
        <list-albums :albums="albums_list" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import webapi from '@/webapi'
import store from '@/store'
import Albums from '@/lib/Albums'

const dataObject = {
  load: function (to) {
    const limit = store.getters.settings_option_recently_added_limit
    return webapi.search({
      type: 'album',
      expression:
        'media_kind is music having track_count > 3 order by time_added desc',
      limit: limit
    })
  },

  set: function (vm, response) {
    vm.recently_added = response.data.albums
  }
}

export default {
  name: 'PageBrowseType',
  components: { ContentWithHeading, TabsMusic, ListAlbums },

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
      recently_added: { items: [] }
    }
  },

  computed: {
    albums_list() {
      return new Albums(this.recently_added.items, {
        hideSingles: false,
        hideSpotify: false,
        sort: 'Recently added (browse)',
        group: true
      })
    }
  }
}
</script>

<style></style>
