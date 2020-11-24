<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Recently added</p>
        <p class="heading">albums</p>
      </template>
      <template slot="content">
        <list-albums :albums="recently_added.items"></list-albums>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import ListAlbums from '@/components/ListAlbums'
import webapi from '@/webapi'

const browseData = {
  load: function (to) {
    return webapi.search({
      type: 'album',
      expression: 'media_kind is music having track_count > 3 order by time_added desc',
      limit: 500
    })
  },

  set: function (vm, response) {
    vm.recently_added = response.data.albums
  }
}

export default {
  name: 'PageBrowseType',
  mixins: [LoadDataBeforeEnterMixin(browseData)],
  components: { ContentWithHeading, TabsMusic, ListAlbums },

  data () {
    return {
      recently_added: {}
    }
  }
}
</script>

<style>
</style>
