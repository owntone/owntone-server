<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Recently added</p>
        <p class="heading">albums</p>
      </template>
      <template slot="content">
        <list-item-album v-for="album in recently_added.items" :key="album.id" :album="album"></list-item-album>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import ListItemAlbum from '@/components/ListItemAlbum'
import webapi from '@/webapi'

const browseData = {
  load: function (to) {
    return webapi.search({
      type: 'album',
      expression: 'time_added after 8 weeks ago having track_count > 3 order by time_added desc',
      limit: 50
    })
  },

  set: function (vm, response) {
    vm.recently_added = response.data.albums
  }
}

export default {
  name: 'PageBrowseType',
  mixins: [ LoadDataBeforeEnterMixin(browseData) ],
  components: { ContentWithHeading, TabsMusic, ListItemAlbum },

  data () {
    return {
      recently_added: {}
    }
  }
}
</script>

<style>
</style>
