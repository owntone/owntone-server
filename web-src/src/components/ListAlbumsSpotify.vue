<template>
  <template v-for="item in items" :key="item.id">
    <div class="media is-align-items-center is-clickable" @click="open(item)">
      <div
        v-if="settingsStore.show_cover_artwork_in_album_lists"
        class="media-left"
      >
        <cover-artwork
          :url="artwork_url(item)"
          :artist="item.artist"
          :album="item.name"
          class="fd-has-shadow fd-cover fd-cover-small-image"
        />
      </div>
      <div class="media-content is-clipped">
        <p class="title is-6" v-text="item.name" />
        <p
          class="subtitle is-7 has-text-grey has-text-weight-bold"
          v-text="item.artists[0]?.name"
        />
        <p
          class="subtitle is-7 has-text-grey"
          v-text="
            [item.album_type, $filters.date(item.release_date)].join(', ')
          "
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
    <modal-dialog-album-spotify
      :item="selected_item"
      :show="show_details_modal"
      @close="show_details_modal = false"
    />
  </teleport>
</template>

<script>
import CoverArtwork from '@/components/CoverArtwork.vue'
import ModalDialogAlbumSpotify from '@/components/ModalDialogAlbumSpotify.vue'
import { useSettingsStore } from '@/stores/settings'

export default {
  name: 'ListAlbumsSpotify',
  components: { CoverArtwork, ModalDialogAlbumSpotify },
  props: { items: { required: true, type: Object } },

  setup() {
    return { settingsStore: useSettingsStore() }
  },

  data() {
    return { selected_item: {}, show_details_modal: false }
  },

  methods: {
    artwork_url(item) {
      return item.images?.[0]?.url ?? ''
    },
    open(item) {
      this.$router.push({
        name: 'music-spotify-album',
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
