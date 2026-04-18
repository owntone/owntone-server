<template>
  <list-item
    v-for="item in items"
    :key="item.itemId"
    :icon="icon(item.item)"
    :is-item="item.isItem"
    :index="item.index"
    :lines="[item.item.name]"
    @open="open(item.item)"
    @open-details="openDetails(item.item)"
  />
  <modal-dialog-playlist
    :item="selectedItem"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import ListItem from '@/components/ListItem.vue'
import ModalDialogPlaylist from '@/components/ModalDialogPlaylist.vue'
import { ref } from 'vue'
import { useRouter } from 'vue-router'

defineProps({
  items: { required: true, type: Object },
  load: { default: null, type: Function }
})

const router = useRouter()

const selectedItem = ref({})
const showDetailsModal = ref(false)

const icon = (item) => {
  if (item.type === 'folder') {
    return 'folder'
  } else if (item.type === 'rss') {
    return 'rss'
  }
  return 'music-box-multiple'
}

const open = (item) => {
  if (item.type === 'folder') {
    router.push({ name: 'playlist-folder', params: { id: item.id } })
  } else {
    router.push({ name: 'playlist', params: { id: item.id } })
  }
}

const openDetails = (item) => {
  selectedItem.value = item
  showDetailsModal.value = true
}
</script>
