<template>
  <div>
    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Radio</p>
      </template>
      <template slot="content">
        <p class="heading has-text-centered-mobile">{{ tracks.total }} tracks</p>
        <list-tracks :tracks="tracks.items"></list-tracks>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import ListTracks from '@/components/ListTracks'
import webapi from '@/webapi'

const streamsData = {
  load: function (to) {
    return webapi.library_radio_streams()
  },

  set: function (vm, response) {
    vm.tracks = response.data.tracks
  }
}

export default {
  name: 'PageRadioStreams',
  mixins: [LoadDataBeforeEnterMixin(streamsData)],
  components: { ContentWithHeading, ListTracks },

  data () {
    return {
      tracks: { items: [] }
    }
  }
}
</script>

<style>
</style>
