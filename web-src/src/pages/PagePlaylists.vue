<template>
  <content-with-heading>
    <template slot="heading-left">
      <p class="title is-4">Playlists</p>
      <p class="heading">{{ playlists.total }} playlists</p>
    </template>
    <template slot="content">
      <list-item-playlist v-for="playlist in playlists.items" :key="playlist.id" :playlist="playlist"></list-item-playlist>
    </template>
  </content-with-heading>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import ListItemPlaylist from '@/components/ListItemPlaylist'
import webapi from '@/webapi'

const playlistsData = {
  load: function (to) {
    return webapi.library_playlists()
  },

  set: function (vm, response) {
    vm.playlists = response.data
  }
}

export default {
  name: 'PagePlaylists',
  mixins: [ LoadDataBeforeEnterMixin(playlistsData) ],
  components: { ContentWithHeading, TabsMusic, ListItemPlaylist },

  data () {
    return {
      playlists: {}
    }
  }
}
</script>

<style>
</style>
