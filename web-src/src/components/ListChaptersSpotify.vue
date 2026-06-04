<template>
  <list-item
    v-for="(item, index) in items"
    :key="item.id"
    :is-playable="item.is_playable !== false"
    :lines="lines(item)"
    @open="open(item, index)"
    @open-details="openDetails(item)"
  >
    <template v-if="item.is_playable === false" #reason>
      (<span v-text="$t('list.spotify.not-playable-track')" />
      <span
        v-if="item.restrictions?.reason"
        v-text="
          $t('list.spotify.restriction-reason', {
            reason: item.restrictions.reason
          })
        "
      />)
    </template>
  </list-item>
  <loader-list-item :load="load" />
  <modal-dialog-track-spotify
    :item="selectedItem"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import ListItem from '@/components/ListItem.vue'
import LoaderListItem from '@/components/LoaderListItem.vue'
import ModalDialogTrackSpotify from '@/components/ModalDialogTrackSpotify.vue'
import queue from '@/api/queue'
import { ref } from 'vue'

const props = defineProps({
  contextUri: { default: '', type: String },
  items: { required: true, type: Object },
  load: { default: null, type: Function }
})
const selectedItem = ref({})
const showDetailsModal = ref(false)

const lines = (item) => [item.name, item.description?.substring(0, 80) ?? '']

const open = (item, index) => {
  if (item.is_playable !== false) {
    queue.playUri(props.contextUri || item.uri, false, index)
  }
}

const openDetails = (item) => {
  selectedItem.value = item
  showDetailsModal.value = true
}
</script>
