<template>
  <template v-for="item in items" :key="item.itemId">
    <div
      class="media is-align-items-center is-clickable mb-0"
      @click="open(item.item)"
    >
      <mdicon class="media-left icon" :name="icon(item.item)" />
      <div class="media-content">
        <p class="is-size-6 has-text-weight-bold" v-text="item.item.name" />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="openDialog(item.item)">
          <mdicon class="icon has-text-grey" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-playlist
      :item="selectedItem"
      :show="showDetailsModal"
      @close="showDetailsModal = false"
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
    return { selectedItem: {}, showDetailsModal: false }
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
    openDialog(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    }
  }
}
</script>
