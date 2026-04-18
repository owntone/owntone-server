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
  <modal-dialog-genre
    :item="selectedItem"
    :media-kind="mediaKind"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import ListItem from '@/components/ListItem.vue'
import ModalDialogGenre from '@/components/ModalDialogGenre.vue'
import { ref } from 'vue'
import { useRouter } from 'vue-router'

const router = useRouter()

const props = defineProps({
  items: { required: true, type: Object },
  mediaKind: { required: true, type: String }
})

const selectedItem = ref({})
const showDetailsModal = ref(false)

const open = (item) => {
  router.push({
    name: 'genre-albums',
    params: { name: item.name },
    query: { mediaKind: props.mediaKind }
  })
}

const openDetails = (item) => {
  selectedItem.value = item
  showDetailsModal.value = true
}
</script>
