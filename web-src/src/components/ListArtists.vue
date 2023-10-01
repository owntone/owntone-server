<template>
  <template v-for="artist in artists" :key="artist.itemId">
    <div v-if="!artist.isItem && !hide_group_title" class="mt-6 mb-5 py-2">
      <div class="media-content is-clipped">
        <span
          :id="'index_' + artist.groupKey"
          class="tag is-info is-light is-small has-text-weight-bold"
          v-text="artist.groupKey"
        />
      </div>
    </div>
    <div
      v-else-if="artist.isItem"
      class="media is-align-items-center"
      @click="open_artist(artist.item)"
    >
      <div class="media-content is-clickable is-clipped">
        <h1 class="title is-6" v-text="artist.item.name" />
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(artist.item)">
          <mdicon class="icon has-text-dark" name="dots-vertical" size="16" />
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-artist
      :artist="selected_artist"
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

  props: ['artists', 'hide_group_title'],

  data() {
    return {
      show_details_modal: false,
      selected_artist: {}
    }
  },

  methods: {
    open_artist(artist) {
      this.selected_artist = artist
      const route =
        artist.media_kind === 'audiobook' ? 'audiobooks-artist' : 'music-artist'
      this.$router.push({ name: route, params: { id: artist.id } })
    },

    open_dialog(artist) {
      this.selected_artist = artist
      this.show_details_modal = true
    }
  }
}
</script>

<style></style>
