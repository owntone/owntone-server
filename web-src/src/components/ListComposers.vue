<template>
  <template v-for="item in items" :key="item.itemId">
    <div v-if="!item.isItem" class="mt-6 mb-5 py-2">
      <div class="media-content is-clipped">
        <span
          :id="`index_${item.index}`"
          class="tag is-info is-light is-small has-text-weight-bold"
          v-text="item.index"
        />
      </div>
    </div>
    <div v-else class="media is-align-items-center" @click="open(item.item)">
      <div class="media-content is-clickable is-clipped">
        <h1 class="title is-6" v-text="item.item.name" />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(item.item)">
          <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-composer
      :item="selected_item"
      :show="show_details_modal"
      @close="show_details_modal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'

export default {
  name: 'ListComposers',
  components: { ModalDialogComposer },
  props: {
    items: { required: true, type: Object }
  },

  data() {
    return {
      selected_item: {},
      show_details_modal: false
    }
  },

  methods: {
    open(item) {
      this.selected_item = item
      this.$router.push({
        name: 'music-composer-albums',
        params: { name: item.name }
      })
    },

    open_dialog(item) {
      this.selected_item = item
      this.show_details_modal = true
    }
  }
}
</script>

<style></style>
