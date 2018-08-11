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
        <list-item-album v-for="album in recently_added.items" :key="album.id" :album="album"></list-item-album>
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
        <list-item-track v-for="track in recently_played.items" :key="track.id" :track="track" :position="0" :context_uri="track.uri"></list-item-track>
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
import ListItemAlbum from '@/components/ListItemAlbum'
import ListItemTrack from '@/components/ListItemTrack'
import webapi from '@/webapi'

const browseData = {
  load: function (to) {
    return Promise.all([
      webapi.search({ type: 'album', expression: 'time_added after 8 weeks ago having track_count > 3 order by time_added desc', limit: 3 }),
      webapi.search({ type: 'track', expression: 'time_played after 8 weeks ago order by time_played desc', limit: 3 })
    ])
  },

  set: function (vm, response) {
    vm.recently_added = response[0].data.albums
    vm.recently_played = response[1].data.tracks
  }
}

export default {
  name: 'PageBrowse',
  mixins: [ LoadDataBeforeEnterMixin(browseData) ],
  components: { ContentWithHeading, TabsMusic, ListItemAlbum, ListItemTrack },

  data () {
    return {
      recently_added: {},
      recently_played: {}
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
