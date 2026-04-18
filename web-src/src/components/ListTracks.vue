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

<script setup>
import ListItem from '@/components/ListItem.vue'
import ModalDialogTrack from '@/components/ModalDialogTrack.vue'
import queue from '@/api/queue'
import { ref } from 'vue'

const props = defineProps({
  expression: { default: '', type: String },
  icon: { default: null, type: String },
  items: { default: null, type: Object },
  load: { default: null, type: Function },
  showProgress: { default: false, type: Boolean },
  uris: { default: '', type: String }
})

defineEmits(['play-count-changed'])

const selectedItem = ref({})
const showDetailsModal = ref(false)

const isRead = (item) => item.media_kind === 'podcast' && item.play_count > 0

const open = (item) => {
  if (props.uris) {
    queue.playUri(props.uris, false, props.items.items.indexOf(item))
  } else if (props.expression) {
    queue.playExpression(
      props.expression,
      false,
      props.items.items.indexOf(item)
    )
  } else {
    queue.playUri(item.uri, false)
  }
}

const openDetails = (item) => {
  selectedItem.value = item
  showDetailsModal.value = true
}

const progress = (item) => {
  if (props.showProgress && item.seek_ms > 0) {
    return item.seek_ms / item.length_ms
  }
  return null
}
</script>
