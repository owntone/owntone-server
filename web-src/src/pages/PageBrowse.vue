<template>
  <div>
    <tabs-music></tabs-music>

    <!-- Recently added -->
    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Recently added</p>
        <p class="heading">albums</p>
      </template>
      <template slot="content">
        <list-albums :albums="recently_added.items"></list-albums>
      </template>
      <template slot="footer">
        <nav class="level">
          <p class="level-item">
            <a class="button is-light is-small is-rounded" v-on:click="open_browse('recently_added')">Show more</a>
          </p>
        </nav>
      </template>
    </content-with-heading>

    <!-- Recently played -->
    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Recently played</p>
        <p class="heading">tracks</p>
      </template>
      <template slot="content">
        <list-tracks :tracks="recently_played.items"></list-tracks>
      </template>
      <template slot="footer">
        <nav class="level">
          <p class="level-item">
            <a class="button is-light is-small is-rounded" v-on:click="open_browse('recently_played')">Show more</a>
          </p>
        </nav>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import ListAlbums from '@/components/ListAlbums'
import ListTracks from '@/components/ListTracks'
import webapi from '@/webapi'

const browseData = {
  load: function (to) {
    return Promise.all([
      webapi.search({ type: 'album', expression: 'time_added after 8 weeks ago and media_kind is music having track_count > 3 order by time_added desc', limit: 3 }),
      webapi.search({ type: 'track', expression: 'time_played after 8 weeks ago and media_kind is music order by time_played desc', limit: 3 })
    ])
  },

  set: function (vm, response) {
    vm.recently_added = response[0].data.albums
    vm.recently_played = response[1].data.tracks
  }
}

export default {
  name: 'PageBrowse',
  mixins: [LoadDataBeforeEnterMixin(browseData)],
  components: { ContentWithHeading, TabsMusic, ListAlbums, ListTracks },

  data () {
    return {
      recently_added: { items: [] },
      recently_played: { items: [] },

      show_track_details_modal: false,
      selected_track: {}
    }
  },

  methods: {
    open_browse: function (type) {
      this.$router.push({ path: '/music/browse/' + type })
    }
  }
}
</script>

<style>
</style>
