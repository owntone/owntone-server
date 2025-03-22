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
  <modal-dialog-genre
    :item="selectedItem"
    :media-kind="mediaKind"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ListItem from '@/components/ListItem.vue'
import ModalDialogGenre from '@/components/ModalDialogGenre.vue'

export default {
  name: 'ListGenres',
  components: { ListItem, ModalDialogGenre },
  props: {
    items: { required: true, type: Object },
    mediaKind: { required: true, type: String }
  },
  data() {
    return { selectedItem: {}, showDetailsModal: false }
  },
  methods: {
    open(item) {
      this.$router.push({
        name: 'genre-albums',
        params: { name: item.name },
        query: { mediaKind: this.mediaKind }
      })
    },
    openDetails(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    }
  }
}
</script>
