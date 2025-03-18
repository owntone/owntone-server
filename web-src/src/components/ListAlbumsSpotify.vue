<template>
  <list-item
    v-for="item in items"
    :key="item.id"
    :is-item="item.isItem"
    :image="image(item)"
    :index="item.index"
    :lines="[
      item.name,
      item.artists[0]?.name,
      $filters.toDate(item.release_date)
    ]"
    @open="open(item)"
    @open-details="openDetails(item)"
  />
  <modal-dialog-album-spotify
    :item="selectedItem"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ListItem from '@/components/ListItem.vue'
import ModalDialogAlbumSpotify from '@/components/ModalDialogAlbumSpotify.vue'
import { useSettingsStore } from '@/stores/settings'

export default {
  name: 'ListAlbumsSpotify',
  components: { ListItem, ModalDialogAlbumSpotify },
  props: { items: { required: true, type: Object } },
  setup() {
    return { settingsStore: useSettingsStore() }
  },
  data() {
    return { selectedItem: {}, showDetailsModal: false }
  },
  methods: {
    image(item) {
      if (this.settingsStore.show_cover_artwork_in_album_lists) {
        return { url: item.images?.[0]?.url ?? '', caption: item.name }
      }
      return null
    },
    open(item) {
      this.$router.push({
        name: 'music-spotify-album',
        params: { id: item.id }
      })
    },
    openDetails(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    }
  }
}
</script>
