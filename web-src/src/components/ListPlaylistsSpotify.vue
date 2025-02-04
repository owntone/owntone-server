<template>
  <template v-for="item in items" :key="item.id">
    <div
      class="media is-align-items-center is-clickable mb-0"
      @click="open(item)"
    >
      <div class="media-content">
        <div class="is-size-6 has-text-weight-bold" v-text="item.name" />
        <div
          class="is-size-7 has-text-weight-bold has-text-grey"
          v-text="item.owner.display_name"
        />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(item)">
          <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-playlist-spotify
      :item="selected_item"
      :show="show_details_modal"
      @close="show_details_modal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogPlaylistSpotify from '@/components/ModalDialogPlaylistSpotify.vue'

export default {
  name: 'ListPlaylistsSpotify',
  components: {
    ModalDialogPlaylistSpotify
  },
  props: { items: { required: true, type: Object } },

  data() {
    return { selected_item: {}, show_details_modal: false }
  },

  methods: {
    open(item) {
      this.$router.push({ name: 'playlist-spotify', params: { id: item.id } })
    },
    open_dialog(item) {
      this.selected_item = item
      this.show_details_modal = true
    }
  }
}
</script>
