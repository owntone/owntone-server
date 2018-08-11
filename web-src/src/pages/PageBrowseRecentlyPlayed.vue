<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Recently played</p>
        <p class="heading">tracks</p>
      </template>
      <template slot="content">
        <list-item-track v-for="track in recently_played.items" :key="track.id" :track="track" :position="0" :context_uri="track.uri"></list-item-track>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import ListItemTrack from '@/components/ListItemTrack'
import webapi from '@/webapi'

const browseData = {
  load: function (to) {
    return webapi.search({
      type: 'track',
      expression: 'time_played after 8 weeks ago order by time_played desc',
      limit: 50
    })
  },

  set: function (vm, response) {
    vm.recently_played = response.data.tracks
  }
}

export default {
  name: 'PageBrowseType',
  mixins: [ LoadDataBeforeEnterMixin(browseData) ],
  components: { ContentWithHeading, TabsMusic, ListItemTrack },

  data () {
    return {
      recently_played: {}
    }
  }
}
</script>

<style>
</style>
