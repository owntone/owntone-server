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
      $formatters.toDate(item.release_date)
    ]"
    @open="open(item)"
    @open-details="openDetails(item)"
  />
  <loader-list-item :load="load" :loaded="loaded" />
  <modal-dialog-album-spotify
    :item="selectedItem"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ListItem from '@/components/ListItem.vue'
import LoaderListItem from '@/components/LoaderListItem.vue'
import ModalDialogAlbumSpotify from '@/components/ModalDialogAlbumSpotify.vue'
import { useSettingsStore } from '@/stores/settings'

export default {
  name: 'ListAlbumsSpotify',
  components: { ListItem, LoaderListItem, ModalDialogAlbumSpotify },
  props: {
    items: { required: true, type: Object },
    load: { default: null, type: Function },
    loaded: { default: true, type: Boolean }
  },
  setup() {
    return { settingsStore: useSettingsStore() }
  },
  data() {
    return { selectedItem: {}, showDetailsModal: false }
  },
  methods: {
    image(item) {
      if (this.settingsStore.showCoverArtworkInAlbumLists) {
        return { caption: item.name, url: item.images?.[0]?.url ?? '' }
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
