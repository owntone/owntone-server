<template>
  <div class="media is-align-items-center">
    <div class="media-content is-clickable is-clipped" @click="open_playlist">
      <h1 class="title is-6" v-text="item.name" />
      <h2 class="subtitle is-7" v-text="item.owner.display_name" />
    </div>
    <div class="media-right">
      <a @click.prevent.stop="show_details_modal = true">
        <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
      </a>
    </div>
  </div>
  <teleport to="#app">
    <modal-dialog-playlist-spotify
      :show="show_details_modal"
      :playlist="item"
      @close="show_details_modal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogPlaylistSpotify from '@/components/ModalDialogPlaylistSpotify.vue'

export default {
  name: 'ListItemPlaylistSpotify',
  components: {
    ModalDialogPlaylistSpotify
  },
  props: { item: { required: true, type: Object } },

  data() {
    return { show_details_modal: false }
  },

  methods: {
    open_playlist() {
      this.$router.push({
        name: 'playlist-spotify',
        params: { id: this.item.id }
      })
    }
  }
}
</script>

<style></style>
