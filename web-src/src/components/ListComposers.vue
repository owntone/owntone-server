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
  <modal-dialog-composer
    :item="selectedItem"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import ListItem from '@/components/ListItem.vue'
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'
import { ref } from 'vue'
import { useRouter } from 'vue-router'

defineProps({
  items: { required: true, type: Object },
  load: { default: null, type: Function }
})

const router = useRouter()

const selectedItem = ref({})
const showDetailsModal = ref(false)

const open = (item) => {
  router.push({ name: 'music-composer-albums', params: { name: item.name } })
}

const openDetails = (item) => {
  selectedItem.value = item
  showDetailsModal.value = true
}
</script>
