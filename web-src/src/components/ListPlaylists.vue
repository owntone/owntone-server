<template>
  <template v-for="item in items" :key="item.itemId">
    <div class="media is-align-items-center" @click="open(item.item)">
      <mdicon class="media-left is-clickable icon" :name="icon(item.item)" />
      <div class="media-content is-clickable is-clipped">
        <p class="title is-6" v-text="item.item.name" />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(item.item)">
          <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-playlist
      :item="selected_item"
      :show="show_details_modal"
      @close="show_details_modal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogPlaylist from '@/components/ModalDialogPlaylist.vue'

export default {
  name: 'ListPlaylists',
  components: { ModalDialogPlaylist },
  props: { items: { required: true, type: Object } },

  data() {
    return {
      selected_item: {},
      show_details_modal: false
    }
  },

  methods: {
    icon(item) {
      if (item.type === 'folder') {
        return 'folder'
      } else if (item.type === 'rss') {
        return 'rss'
      }
      return 'music-box-multiple'
    },
    open(item) {
      if (item.type === 'folder') {
        this.$router.push({ name: 'playlist-folder', params: { id: item.id } })
      } else {
        this.$router.push({ name: 'playlist', params: { id: item.id } })
      }
    },
    open_dialog(item) {
      this.selected_item = item
      this.show_details_modal = true
    }
  }
}
</script>
