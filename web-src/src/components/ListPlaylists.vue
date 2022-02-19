<template>
  <div>
    <list-item-playlist v-for="playlist in playlists" :key="playlist.id" :playlist="playlist" @click="open_playlist(playlist)">
      <template v-slot:icon>
        <span class="icon">
          <i class="mdi" :class="{ 'mdi-library-music': playlist.type !== 'folder', 'mdi-rss': playlist.type === 'rss', 'mdi-folder': playlist.type === 'folder' }"></i>
        </span>
      </template>
      <template v-slot:actions>
        <a @click.prevent.stop="open_dialog(playlist)">
          <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
        </a>
      </template>
    </list-item-playlist>
    <modal-dialog-playlist :show="show_details_modal" :playlist="selected_playlist" @close="show_details_modal = false" />
  </div>
</template>

<script>
import ListItemPlaylist from '@/components/ListItemPlaylist.vue'
import ModalDialogPlaylist from '@/components/ModalDialogPlaylist.vue'

export default {
  name: 'ListPlaylists',
  components: { ListItemPlaylist, ModalDialogPlaylist },

  props: ['playlists'],

  data () {
    return {
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
