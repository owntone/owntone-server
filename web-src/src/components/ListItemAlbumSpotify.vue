<template>
  <div class="media is-align-items-center" @click="open">
    <div v-if="show_artwork" class="media-left is-clickable">
      <cover-artwork
        :artwork_url="artwork_url"
        :artist="item.artist"
        :album="item.name"
        class="is-clickable fd-has-shadow fd-cover fd-cover-small-image"
      />
    </div>
    <div class="media-content is-clickable is-clipped">
      <h1 class="title is-6" v-text="item.name" />
      <h2
        class="subtitle is-7 has-text-grey has-text-weight-bold"
        v-text="item.artists[0]?.name"
      />
      <h2
        class="subtitle is-7 has-text-grey"
        v-text="[item.album_type, $filters.date(item.release_date)].join(', ')"
      />
    </div>
    <div class="media-right">
      <a @click.prevent.stop="show_details_modal = true">
        <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
      </a>
    </div>
  </div>
  <teleport to="#app">
    <modal-dialog-album-spotify
      :show="show_details_modal"
      :album="item"
      @close="show_details_modal = false"
    />
  </teleport>
</template>

<script>
import CoverArtwork from '@/components/CoverArtwork.vue'
import ModalDialogAlbumSpotify from '@/components/ModalDialogAlbumSpotify.vue'

export default {
  name: 'ListItemAlbumSpotify',
  components: { CoverArtwork, ModalDialogAlbumSpotify },
  props: { item: { required: true, type: Object } },

  data() {
    return {
      show_details_modal: false
    }
  },

  computed: {
    artwork_url() {
      return this.item.images?.[0]?.url ?? ''
    },
    show_artwork() {
      return this.$store.getters.settings_option(
        'webinterface',
        'show_cover_artwork_in_album_lists'
      ).value
    }
  },

  methods: {
    open() {
      this.$router.push({
        name: 'music-spotify-album',
        params: { id: this.item.id }
      })
    }
  }
}
</script>

<style></style>
