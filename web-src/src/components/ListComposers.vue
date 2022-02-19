<template>
  <div>
    <div v-if="is_grouped">
      <div v-for="idx in composers.indexList" :key="idx" class="mb-6">
        <span
          :id="'index_' + idx"
          class="tag is-info is-light is-small has-text-weight-bold"
          >{{ idx }}</span
        >
        <list-item-composer
          v-for="composer in composers.grouped[idx]"
          :key="composer.id"
          :composer="composer"
          @click="open_composer(composer)"
        >
          <template #actions>
            <a @click.prevent.stop="open_dialog(composer)">
              <span class="icon has-text-dark"
                ><i class="mdi mdi-dots-vertical mdi-18px"
              /></span>
            </a>
          </template>
        </list-item-composer>
      </div>
    </div>
    <div v-else>
      <list-item-composer
        v-for="composer in composers_list"
        :key="composer.id"
        :composer="composer"
        @click="open_composer(composer)"
      >
        <template #actions>
          <a @click.prevent.stop="open_dialog(composer)">
            <span class="icon has-text-dark"
              ><i class="mdi mdi-dots-vertical mdi-18px"
            /></span>
          </a>
        </template>
      </list-item-composer>
    </div>
    <modal-dialog-composer
      :show="show_details_modal"
      :composer="selected_composer"
      :media_kind="media_kind"
      @close="show_details_modal = false"
    />
  </div>
</template>

<script>
import ListItemComposer from '@/components/ListItemComposer.vue'
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'
import Composers from '@/lib/Composers'

export default {
  name: 'ListComposers',
  components: { ListItemComposer, ModalDialogComposer },

  props: ['composers', 'media_kind'],

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
    },

    composers_list: function () {
      if (Array.isArray(this.composers)) {
        return this.composers
      }
      return this.composers.sortedAndFiltered
    },

    is_grouped: function () {
      return this.composers instanceof Composers && this.composers.options.group
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
