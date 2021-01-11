<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Recently added</p>
        <p class="heading">albums</p>
      </template>
      <template slot="content">
        <list-albums :albums="albums_list"></list-albums>
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
import store from '@/store'
import Albums from '@/lib/Albums'

const browseData = {
  load: function (to) {
    const limit = store.getters.settings_option_recently_added_limit
    return webapi.search({
      type: 'album',
      expression: 'media_kind is music having track_count > 3 order by time_added desc',
      limit: limit
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
      recently_added: { items: [] }
    }
  },

  computed: {
    albums_list () {
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

<style>
</style>
