<template>
  <content-with-heading>
    <template slot="heading-left">
      <p class="title is-4">Playlists</p>
      <p class="heading">{{ playlists.total }} playlists</p>
    </template>
    <template slot="content">
      <list-item-playlist v-for="playlist in playlists.items" :key="playlist.id" :playlist="playlist" @click="open_playlist(playlist)">
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
    return webapi.library_playlists()
  },

  set: function (vm, response) {
    vm.playlists = response.data
  }
}

export default {
  name: 'PagePlaylists',
  mixins: [ LoadDataBeforeEnterMixin(playlistsData) ],
  components: { ContentWithHeading, TabsMusic, ListItemPlaylist, ModalDialogPlaylist },

  data () {
    return {
      playlists: {},

      show_details_modal: false,
      selected_playlist: {}
    }
  },

  methods: {
    open_playlist: function (playlist) {
      this.$router.push({ path: '/playlists/' + playlist.id })
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
