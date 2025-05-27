<template>
  <list-item
    v-for="item in items"
    :key="item.itemId"
    :icon="icon"
    :is-item="item.isItem"
    :is-read="isRead(item.item)"
    :index="item.index"
    :lines="[item.item.title, item.item.artist, item.item.album]"
    :progress="progress(item.item)"
    @open="open(item.item)"
    @open-details="openDetails(item.item)"
  />
  <modal-dialog-track
    :item="selectedItem"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
    @play-count-changed="$emit('play-count-changed')"
  />
</template>

<script>
import ListItem from '@/components/ListItem.vue'
import ModalDialogTrack from '@/components/ModalDialogTrack.vue'
import queue from '@/api/queue'

export default {
  name: 'ListTracks',
  components: { ListItem, ModalDialogTrack },
  props: {
    expression: { default: '', type: String },
    icon: { default: null, type: String },
    items: { default: null, type: Object },
    load: { default: null, type: Function },
    showProgress: { default: false, type: Boolean },
    uris: { default: '', type: String }
  },
  emits: ['play-count-changed'],
  data() {
    return { selectedItem: {}, showDetailsModal: false }
  },
  methods: {
    isRead(item) {
      return item.media_kind === 'podcast' && item.play_count > 0
    },
    open(item) {
      if (this.uris) {
        queue.playUri(this.uris, false, this.items.items.indexOf(item))
      } else if (this.expression) {
        queue.playExpression(
          this.expression,
          false,
          this.items.items.indexOf(item)
        )
      } else {
        queue.playUri(item.uri, false)
      }
    },
    openDetails(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    },
    progress(item) {
      if (this.showProgress && item.seek_ms > 0) {
        return item.seek_ms / item.length_ms
      }
      return null
    }
  }
}
</script>
