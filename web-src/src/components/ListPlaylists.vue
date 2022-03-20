<template>
  <div
    v-for="playlist in playlists"
    :key="playlist.id"
    class="media"
    :playlist="playlist"
    @click="open_playlist(playlist)"
  >
    <figure class="media-left fd-has-action">
      <span class="icon">
        <i
          class="mdi"
          :class="{
            'mdi-library-music': playlist.type !== 'folder',
            'mdi-rss': playlist.type === 'rss',
            'mdi-folder': playlist.type === 'folder'
          }"
        />
      </span>
    </figure>
    <div class="media-content fd-has-action is-clipped">
      <h1 class="title is-6">
        {{ playlist.name }}
      </h1>
    </div>
    <div class="media-right">
      <a @click.prevent.stop="open_dialog(playlist)">
        <span class="icon has-text-dark"
          ><i class="mdi mdi-dots-vertical mdi-18px"
        /></span>
      </a>
    </div>
  </div>
  <teleport to="#app">
    <modal-dialog-playlist
      :show="show_details_modal"
      :playlist="selected_playlist"
      @close="show_details_modal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogPlaylist from '@/components/ModalDialogPlaylist.vue'

export default {
  name: 'ListPlaylists',
  components: { ModalDialogPlaylist },

  props: ['playlists'],

  data() {
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

<style></style>
