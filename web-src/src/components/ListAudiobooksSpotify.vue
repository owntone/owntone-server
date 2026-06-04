<template>
  <list-item
    v-for="item in items"
    :key="item.id"
    :is-item="item.isItem"
    :image="image(item)"
    :index="item.index"
    :lines="[item.name, item.authors?.[0]?.name, item.edition]"
    @open="open(item)"
    @open-details="openDetails(item)"
  />
  <loader-list-item :load="load" />
  <modal-dialog-audiobook-spotify
    :item="selectedItem"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import ListItem from '@/components/ListItem.vue'
import LoaderListItem from '@/components/LoaderListItem.vue'
import ModalDialogAudiobookSpotify from '@/components/ModalDialogAudiobookSpotify.vue'
import { ref } from 'vue'
import { useRouter } from 'vue-router'
import { useSettingsStore } from '@/stores/settings'

defineProps({
  items: { required: true, type: Object },
  load: { default: null, type: Function }
})

const settingsStore = useSettingsStore()
const router = useRouter()
const selectedItem = ref({})
const showDetailsModal = ref(false)

const image = (item) => {
  if (settingsStore.showCoverArtworkInAlbumLists) {
    return {
      caption: item.name,
      url: item.images?.[0]?.url ?? ''
    }
  }
  return null
}

const open = (item) => {
  router.push({
    name: 'audiobook-spotify-album',
    params: { id: item.id }
  })
}

const openDetails = (item) => {
  selectedItem.value = item
  showDetailsModal.value = true
}
</script>
