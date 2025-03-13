<template>
  <list-item
    v-for="item in items"
    :key="item.itemId"
    :icon="icon"
    :is-item="item.isItem"
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
import webapi from '@/webapi'

export default {
  name: 'ListTracks',
  components: { ListItem, ModalDialogTrack },
  props: {
    expression: { default: '', type: String },
    items: { default: null, type: Object },
    icon: { default: null, type: String },
    showProgress: { default: false, type: Boolean },
    uris: { default: '', type: String }
  },
  emits: ['play-count-changed'],
  data() {
    return { selectedItem: {}, showDetailsModal: false }
  },
  methods: {
    open(item) {
      if (this.uris) {
        webapi.player_play_uri(this.uris, false, this.items.items.indexOf(item))
      } else if (this.expression) {
        webapi.player_play_expression(
          this.expression,
          false,
          this.items.items.indexOf(item)
        )
      } else {
        webapi.player_play_uri(item.uri, false)
      }
    },
    openDetails(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    },
    //             'has-text-grey': item.item.media_kind === 'podcast' && item.item.play_count > 0
    progress(item) {
      if (item.item) {
        if (this.showProgress && item.item.seek_ms > 0) {
          return item.item.seek_ms / item.item.length_ms
        }
      }
      return null
    }
  }
}
</script>
