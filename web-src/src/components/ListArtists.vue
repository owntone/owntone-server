<template>
  <list-item
    v-for="item in items"
    :key="item.itemId"
    :is-item="item.isItem"
    :index="item.index"
    :lines="[item.item.name]"
    @open="open(item.item)"
    @open-details="openDetails(item.item)"
  />
  <modal-dialog-artist
    :item="selectedItem"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ListItem from '@/components/ListItem.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'

export default {
  name: 'ListArtists',
  components: { ListItem, ModalDialogArtist },
  props: { items: { required: true, type: Object } },
  data() {
    return { selectedItem: {}, showDetailsModal: false }
  },
  methods: {
    open(item) {
      this.selectedItem = item
      const route =
        item.media_kind === 'audiobook' ? 'audiobooks-artist' : 'music-artist'
      this.$router.push({ name: route, params: { id: item.id } })
    },
    openDetails(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    }
  }
}
</script>
