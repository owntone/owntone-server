<template>
  <template v-for="composer in items" :key="composer.itemId">
    <div v-if="!composer.isItem" class="mt-6 mb-5 py-2">
      <div class="media-content is-clipped">
        <span
          :id="'index_' + composer.index"
          class="tag is-info is-light is-small has-text-weight-bold"
          v-text="composer.index"
        />
      </div>
    </div>
    <div
      v-else
      class="media is-align-items-center"
      @click="open_composer(composer.item)"
    >
      <div class="media-content is-clickable is-clipped">
        <h1 class="title is-6" v-text="composer.item.name" />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(composer.item)">
          <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-composer
      :show="show_details_modal"
      :composer="selected_composer"
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
      selected_composer: {},
      show_details_modal: false
    }
  },

  methods: {
    open_composer(composer) {
      this.selected_composer = composer
      this.$router.push({
        name: 'music-composer-albums',
        params: { name: composer.name }
      })
    },

    open_dialog(composer) {
      this.selected_composer = composer
      this.show_details_modal = true
    }
  }
}
</script>

<style></style>
