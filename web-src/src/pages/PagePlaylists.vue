<template>
  <content-with-heading>
    <template slot="heading-left">
      <p class="title is-4">{{ playlist.name }}</p>
      <p class="heading">{{ playlists.total }} playlists</p>
    </template>
    <template slot="content">
      <list-playlists :playlists="playlists.items"></list-playlists>
    </template>
  </content-with-heading>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import ListPlaylists from '@/components/ListPlaylists'
import webapi from '@/webapi'

const playlistsData = {
  load: function (to) {
    return Promise.all([
      webapi.library_playlist(to.params.playlist_id),
      webapi.library_playlist_folder(to.params.playlist_id)
    ])
  },

  set: function (vm, response) {
    vm.playlist = response[0].data
    vm.playlists = response[1].data
  }
}

export default {
  name: 'PagePlaylists',
  mixins: [LoadDataBeforeEnterMixin(playlistsData)],
  components: { ContentWithHeading, ListPlaylists },

  data () {
    return {
      playlist: {},
      playlists: {}
    }
  }
}
</script>

<style>
</style>
