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
  <modal-dialog-artist
    :item="selectedItem"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import ListItem from '@/components/ListItem.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
import { ref } from 'vue'
import { useRouter } from 'vue-router'

defineProps({
  items: { required: true, type: Object },
  load: { default: null, type: Function }
})

const selectedItem = ref({})
const showDetailsModal = ref(false)

const router = useRouter()

const open = (item) => {
  router.push({ name: `${item.media_kind}-artist`, params: { id: item.id } })
}

const openDetails = (item) => {
  selectedItem.value = item
  showDetailsModal.value = true
}
</script>
