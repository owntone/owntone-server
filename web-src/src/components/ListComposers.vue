<template>
  <template v-for="item in items" :key="item.itemId">
    <div v-if="!item.isItem" class="py-5">
      <div class="media-content">
        <span
          :id="`index_${item.index}`"
          class="tag is-small has-text-weight-bold"
          v-text="item.index"
        />
      </div>
    </div>
    <div
      v-else
      class="media is-align-items-center is-clickable mb-0"
      @click="open(item.item)"
    >
      <div class="media-content">
        <p class="is-size-6 has-text-weight-bold" v-text="item.item.name" />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="openDialog(item.item)">
          <mdicon class="icon has-text-grey" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-composer
      :item="selectedItem"
      :show="showDetailsModal"
      @close="showDetailsModal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'

export default {
  name: 'ListComposers',
  components: { ModalDialogComposer },
  props: { items: { required: true, type: Object } },

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

    openDialog(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    }
  }
}
</script>
