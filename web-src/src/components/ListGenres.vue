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
    <modal-dialog-genre
      :item="selectedItem"
      :media_kind="media_kind"
      :show="showDetailsModal"
      @close="showDetailsModal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogGenre from '@/components/ModalDialogGenre.vue'

export default {
  name: 'ListGenres',
  components: { ModalDialogGenre },
  props: {
    items: { required: true, type: Object },
    media_kind: { required: true, type: String }
  },
  data() {
    return { selectedItem: {}, showDetailsModal: false }
  },
  methods: {
    open(item) {
      this.$router.push({
        name: 'genre-albums',
        params: { name: item.name },
        query: { media_kind: this.media_kind }
      })
    },
    openDialog(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    }
  }
}
</script>
