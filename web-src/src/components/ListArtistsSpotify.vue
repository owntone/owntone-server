<template>
  <template v-for="item in items" :key="item.id">
    <div class="media is-align-items-center mb-0">
      <div class="media-content is-clickable" @click="open(item)">
        <p class="title is-6" v-text="item.name" />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(item)">
          <mdicon class="icon has-text-grey" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-artist-spotify
      :item="selected_item"
      :show="show_details_modal"
      @close="show_details_modal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogArtistSpotify from '@/components/ModalDialogArtistSpotify.vue'

export default {
  name: 'ListArtistsSpotify',
  components: { ModalDialogArtistSpotify },
  props: { items: { required: true, type: Object } },

  data() {
    return { selected_item: {}, show_details_modal: false }
  },
  methods: {
    open(item) {
      this.$router.push({
        name: 'music-spotify-artist',
        params: { id: item.id }
      })
    },
    open_dialog(item) {
      this.selected_item = item
      this.show_details_modal = true
    }
  }
}
</script>
