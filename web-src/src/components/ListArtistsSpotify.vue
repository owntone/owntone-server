<template>
  <list-item
    v-for="item in items"
    :key="item.id"
    :is-item="true"
    :image="image(item)"
    :index="item.index"
    :lines="[item.name]"
    @open="open(item)"
    @open-details="openDetails(item)"
  />
  <loader-list-item :load="load" />
  <modal-dialog-artist-spotify
    :item="selectedItem"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import ListItem from '@/components/ListItem.vue'
import LoaderListItem from '@/components/LoaderListItem.vue'
import ModalDialogArtistSpotify from '@/components/ModalDialogArtistSpotify.vue'
import { ref } from 'vue'
import { useRouter } from 'vue-router'

defineProps({
  items: { required: true, type: Object },
  load: { default: null, type: Function }
})

const router = useRouter()

const selectedItem = ref({})
const showDetailsModal = ref(false)

const image = (item) => ({ caption: item.name, url: item.images?.[0]?.url })

const open = (item) => {
  router.push({
    name: 'music-spotify-artist',
    params: { id: item.id }
  })
}

const openDetails = (item) => {
  selectedItem.value = item
  showDetailsModal.value = true
}
</script>
