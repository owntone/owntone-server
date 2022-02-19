<template>
  <div>
    <div v-if="is_grouped">
      <div v-for="idx in artists.indexList" :key="idx" class="mb-6">
        <span
          :id="'index_' + idx"
          class="tag is-info is-light is-small has-text-weight-bold"
          >{{ idx }}</span
        >
        <list-item-artist
          v-for="artist in artists.grouped[idx]"
          :key="artist.id"
          :artist="artist"
          @click="open_artist(artist)"
        >
          <template #actions>
            <a @click.prevent.stop="open_dialog(artist)">
              <span class="icon has-text-dark"
                ><i class="mdi mdi-dots-vertical mdi-18px"
              /></span>
            </a>
          </template>
        </list-item-artist>
      </div>
    </div>
    <div v-else>
      <list-item-artist
        v-for="artist in artists_list"
        :key="artist.id"
        :artist="artist"
        @click="open_artist(artist)"
      >
        <template #actions>
          <a @click.prevent.stop="open_dialog(artist)">
            <span class="icon has-text-dark"
              ><i class="mdi mdi-dots-vertical mdi-18px"
            /></span>
          </a>
        </template>
      </list-item-artist>
    </div>
    <modal-dialog-artist
      :show="show_details_modal"
      :artist="selected_artist"
      :media_kind="media_kind"
      @close="show_details_modal = false"
    />
  </div>
</template>

<script>
import ListItemArtist from '@/components/ListItemArtist.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
import Artists from '@/lib/Artists'

export default {
  name: 'ListArtists',
  components: { ListItemArtist, ModalDialogArtist },

  props: ['artists', 'media_kind'],

  data() {
    return {
      show_details_modal: false,
      selected_artist: {}
    }
  },

  computed: {
    media_kind_resolved: function () {
      return this.media_kind ? this.media_kind : this.selected_artist.media_kind
    },

    artists_list: function () {
      if (Array.isArray(this.artists)) {
        return this.artists
      }
      return this.artists.sortedAndFiltered
    },

    is_grouped: function () {
      return this.artists instanceof Artists && this.artists.options.group
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
