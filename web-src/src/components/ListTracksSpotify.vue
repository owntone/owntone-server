<template>
  <list-item
    v-for="item in items"
    :key="item.id"
    :is-playable="item.is_playable"
    :lines="[item.name, item.artists[0].name, item.album.name]"
    @open="open(item)"
    @open-details="openDetails(item)"
  >
    <template v-if="!item.is_playable" #reason>
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

const open = (item) => {
  if (item.is_playable) {
    queue.playUri(props.contextUri || item.uri, false, item.position || 0)
  }
}

const openDetails = (item) => {
  selectedItem.value = item
  showDetailsModal.value = true
}
</script>
