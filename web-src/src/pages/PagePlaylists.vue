<template>
  <content-with-heading>
    <template slot="heading-left">
      <p class="title is-4">{{ playlist.name }}</p>
      <p class="heading">{{ playlists.total }} playlists</p>
    </template>
    <template slot="content">
      <list-item-playlist v-for="playlist in playlists.items" :key="playlist.id" :playlist="playlist" @click="open_playlist(playlist)">
        <template slot="icon">
          <span class="icon">
            <i class="mdi" :class="{ 'mdi-library-music': playlist.type !== 'folder', 'mdi-rss': playlist.type === 'rss', 'mdi-folder': playlist.type === 'folder' }"></i>
          </span>
        </template>
        <template slot="actions">
          <a @click="open_dialog(playlist)">
            <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
          </a>
        </template>
      </list-item-playlist>
      <modal-dialog-playlist :show="show_details_modal" :playlist="selected_playlist" @close="show_details_modal = false" />
    </template>
  </content-with-heading>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import ListItemPlaylist from '@/components/ListItemPlaylist'
import ModalDialogPlaylist from '@/components/ModalDialogPlaylist'
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
  mixins: [ LoadDataBeforeEnterMixin(playlistsData) ],
  components: { ContentWithHeading, TabsMusic, ListItemPlaylist, ModalDialogPlaylist },

  data () {
    return {
      playlist: {},
      playlists: {},

      show_details_modal: false,
      selected_playlist: {}
    }
  },

  methods: {
    open_playlist: function (playlist) {
      if (playlist.type !== 'folder') {
        this.$router.push({ path: '/playlists/' + playlist.id + '/tracks' })
      } else {
        this.$router.push({ path: '/playlists/' + playlist.id })
      }
    },

    open_dialog: function (playlist) {
      this.selected_playlist = playlist
      this.show_details_modal = true
    }
  }
}
</script>

<style>
</style>
