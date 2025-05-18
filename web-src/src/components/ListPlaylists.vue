<template>
  <list-item
    v-for="item in items"
    :key="item.itemId"
    :icon="icon(item.item)"
    :is-item="item.isItem"
    :index="item.index"
    :lines="[item.item.name]"
    @open="open(item.item)"
    @open-details="openDetails(item.item)"
  />
  <modal-dialog-playlist
    :item="selectedItem"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ListItem from '@/components/ListItem.vue'
import ModalDialogPlaylist from '@/components/ModalDialogPlaylist.vue'

export default {
  name: 'ListPlaylists',
  components: { ListItem, ModalDialogPlaylist },
  props: {
    items: { required: true, type: Object },
    load: { default: null, type: Function },
    loaded: { default: true, type: Boolean }
  },
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
    openDetails(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    }
  }
}
</script>
