<template>
  <template v-for="composer in composers" :key="composer.itemId">
    <div v-if="!composer.isItem && !hide_group_title" class="mt-6 mb-5 py-2">
      <div class="media-content is-clipped">
        <span
          :id="'index_' + composer.groupKey"
          class="tag is-info is-light is-small has-text-weight-bold"
          >{{ composer.groupKey }}</span
        >
      </div>
    </div>
    <div
      v-else-if="composer.isItem"
      class="media"
      @click="open_composer(composer.item)"
    >
      <div class="media-content fd-has-action is-clipped">
        <h1 class="title is-6">
          {{ composer.item.name }}
        </h1>
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(composer.item)">
          <span class="icon has-text-dark"
            ><i class="mdi mdi-dots-vertical mdi-18px"
          /></span>
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-composer
      :show="show_details_modal"
      :composer="selected_composer"
      :media_kind="media_kind"
      @close="show_details_modal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'

export default {
  name: 'ListComposers',
  components: { ModalDialogComposer },

  props: ['composers', 'media_kind', 'hide_group_title'],

  data() {
    return {
      show_details_modal: false,
      selected_composer: {}
    }
  },

  computed: {
    media_kind_resolved: function () {
      return this.media_kind
        ? this.media_kind
        : this.selected_composer.media_kind
    }
  },

  methods: {
    open_composer: function (composer) {
      this.selected_composer = composer
      this.$router.push({
        name: 'ComposerTracks',
        params: { composer: composer.name }
      })
    },

    open_dialog: function (composer) {
      this.selected_composer = composer
      this.show_details_modal = true
    }
  }
}
</script>

<style></style>
