<template>
  <list-item
    v-for="item in items"
    :key="item.itemId"
    :is-item="item.isItem"
    :image="image(item)"
    :index="item.index"
    :lines="[
      item.item.name,
      item.item.artist,
      $formatters.toDate(item.item.date_released)
    ]"
    @open="open(item.item)"
    @open-details="openDetails(item.item)"
  />
  <modal-dialog-album
    :item="selectedItem"
    :media-kind="mediaKind"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
    @play-count-changed="playCountChanged"
    @podcast-deleted="podcastDeleted"
  />
</template>

<script setup>
import ListItem from '@/components/ListItem.vue'
import ModalDialogAlbum from '@/components/ModalDialogAlbum.vue'
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { useSettingsStore } from '@/stores/settings'

const props = defineProps({
  items: { required: true, type: Object },
  load: { default: null, type: Function },
  mediaKind: { default: '', type: String }
})

const emit = defineEmits(['play-count-changed', 'podcast-deleted'])

const settingsStore = useSettingsStore()
const router = useRouter()

const selectedItem = ref({})
const showDetailsModal = ref(false)

const image = (item) => {
  if (settingsStore.showCoverArtworkInAlbumLists) {
    return { caption: item.item.name, url: item.item.artwork_url }
  }
  return null
}

const open = (item) => {
  const mediaKind = props.mediaKind || item.media_kind
  if (mediaKind === 'podcast') {
    router.push({ name: 'podcast', params: { id: item.id } })
  } else {
    router.push({ name: `${mediaKind}-album`, params: { id: item.id } })
  }
}

const openDetails = (item) => {
  selectedItem.value = item
  showDetailsModal.value = true
}

const playCountChanged = () => {
  emit('play-count-changed')
}

const podcastDeleted = () => {
  emit('podcast-deleted')
}
</script>
