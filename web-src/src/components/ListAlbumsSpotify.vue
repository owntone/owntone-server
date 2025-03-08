<template>
  <template v-for="item in items" :key="item.id">
    <div
      class="media is-align-items-center is-clickable mb-0"
      @click="open(item)"
    >
      <control-image
        v-if="settingsStore.show_cover_artwork_in_album_lists"
        :url="item.images?.[0]?.url ?? ''"
        :artist="item.artist"
        :album="item.name"
        class="media-left is-small"
      />
      <div class="media-content">
        <div class="is-size-6 has-text-weight-bold" v-text="item.name" />
        <div
          class="is-size-7 has-text-weight-bold has-text-grey"
          v-text="item.artists[0]?.name"
        />
        <div
          class="is-size-7 has-text-grey"
          v-text="$filters.toDate(item.release_date)"
        />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="openDialog(item)">
          <mdicon class="icon has-text-grey" name="dots-vertical" size="16" />
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
import ControlImage from '@/components/ControlImage.vue'
import ModalDialogAlbumSpotify from '@/components/ModalDialogAlbumSpotify.vue'
import { useSettingsStore } from '@/stores/settings'

export default {
  name: 'ListAlbumsSpotify',
  components: { ControlImage, ModalDialogAlbumSpotify },
  props: { items: { required: true, type: Object } },
  setup() {
    return { settingsStore: useSettingsStore() }
  },
  data() {
    return { selected_item: {}, show_details_modal: false }
  },
  methods: {
    open(item) {
      this.$router.push({
        name: 'music-spotify-album',
        params: { id: item.id }
      })
    },
    openDialog(item) {
      this.selected_item = item
      this.show_details_modal = true
    }
  }
}
</script>
