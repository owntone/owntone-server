<template>
  <template v-for="artist in artists" :key="artist.itemId">
    <div v-if="!artist.isItem && !hide_group_title" class="mt-6 mb-5 py-2">
      <div class="media-content is-clipped">
        <span
          :id="'index_' + artist.groupKey"
          class="tag is-info is-light is-small has-text-weight-bold"
          >{{ artist.groupKey }}</span
        >
      </div>
    </div>
    <div
      v-else-if="artist.isItem"
      class="media"
      @click="open_artist(artist.item)"
    >
      <div class="media-content fd-has-action is-clipped">
        <h1 class="title is-6">
          {{ artist.item.name }}
        </h1>
      </div>
      <div class="media-right">
        <a @click.prevent.stop="open_dialog(artist.item)">
          <span class="icon has-text-dark"
            ><i class="mdi mdi-dots-vertical mdi-18px"
          /></span>
        </a>
      </div>
    </div>
  </template>
  <teleport to="#app">
    <modal-dialog-artist
      :show="show_details_modal"
      :artist="selected_artist"
      :media_kind="media_kind"
      @close="show_details_modal = false"
    />
  </teleport>
</template>

<script>
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'

export default {
  name: 'ListArtists',
  components: { ModalDialogArtist },

  props: ['artists', 'media_kind', 'hide_group_title'],

  data() {
    return {
      show_details_modal: false,
      selected_artist: {}
    }
  },

  computed: {
    media_kind_resolved: function () {
      return this.media_kind ? this.media_kind : this.selected_artist.media_kind
    }
  },

  methods: {
    open_artist: function (artist) {
      this.selected_artist = artist
      if (this.media_kind_resolved === 'podcast') {
        // No artist page for podcasts
      } else if (this.media_kind_resolved === 'audiobook') {
        this.$router.push({ path: '/audiobooks/artists/' + artist.id })
      } else {
        this.$router.push({ path: '/music/artists/' + artist.id })
      }
    },

    open_dialog: function (artist) {
      this.selected_artist = artist
      this.show_details_modal = true
    }
  }
}
</script>

<style></style>
