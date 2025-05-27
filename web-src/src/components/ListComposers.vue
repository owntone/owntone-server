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

<script>
import ListItem from '@/components/ListItem.vue'
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'

export default {
  name: 'ListComposers',
  components: { ListItem, ModalDialogComposer },
  props: {
    items: { required: true, type: Object },
    load: { default: null, type: Function }
  },
  data() {
    return { selectedItem: {}, showDetailsModal: false }
  },
  methods: {
    open(item) {
      this.selectedItem = item
      this.$router.push({
        name: 'music-composer-albums',
        params: { name: item.name }
      })
    },
    openDetails(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    }
  }
}
</script>
