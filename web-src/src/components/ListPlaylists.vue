<template>
  <div
    v-for="playlist in playlists"
    :key="playlist.itemId"
    class="media is-align-items-center"
    @click="open_playlist(playlist.item)"
  >
    <figure class="media-left is-clickable">
      <mdicon class="icon" :name="icon_name(playlist.item)" size="16" />
    </figure>
    <div class="media-content is-clickable is-clipped">
      <h1 class="title is-6" v-text="playlist.item.name" />
    </div>
    <div class="media-right">
      <a @click.prevent.stop="open_dialog(playlist.item)">
        <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
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
    open_playlist(playlist) {
      if (playlist.type === 'folder') {
        this.$router.push({
          name: 'playlist-folder',
          params: { id: playlist.id }
        })
      } else {
        this.$router.push({
          name: 'playlist',
          params: { id: playlist.id }
        })
      }
    },

    open_dialog(playlist) {
      this.selected_playlist = playlist
      this.show_details_modal = true
    },

    icon_name(playlist) {
      if (playlist.type === 'folder') {
        return 'folder'
      } else if (playlist.type === 'rss') {
        return 'rss'
      } else {
        return 'music-box-multiple'
      }
    }
  }
}
</script>

<style></style>
