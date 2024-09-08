<template>
  <template v-for="item in items" :key="item.itemId">
    <div v-if="!item.isItem" class="py-5">
      <div class="media-content">
        <span
          :id="`index_${item.index}`"
          class="tag is-info is-light is-small has-text-weight-bold"
          v-text="item.index"
        />
      </div>
    </div>
    <div v-else class="media is-align-items-center" @click="open(item.item)">
      <div class="media-content is-clickable is-clipped">
        <p class="title is-6" v-text="item.item.name" />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(item.item)">
          <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-artist
      :item="selected_item"
      :show="show_details_modal"
      @close="show_details_modal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'

export default {
  name: 'ListArtists',
  components: { ModalDialogArtist },
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
      const route =
        item.media_kind === 'audiobook' ? 'audiobooks-artist' : 'music-artist'
      this.$router.push({ name: route, params: { id: item.id } })
    },
    open_dialog(item) {
      this.selected_item = item
      this.show_details_modal = true
    }
  }
}
</script>
