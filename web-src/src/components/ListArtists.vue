<template>
  <div>
    <list-item-artist v-for="artist in artists"
        :key="artist.id"
        :artist="artist"
        @click="open_artist(artist)">
        <template slot="actions">
            <a @click="open_dialog(artist)">
            <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
        </template>
    </list-item-artist>
    <modal-dialog-artist :show="show_details_modal" :artist="selected_artist" :media_kind="media_kind" @close="show_details_modal = false" />
  </div>
</template>

<script>
import ListItemArtist from '@/components/ListItemArtist'
import ModalDialogArtist from '@/components/ModalDialogArtist'

export default {
  name: 'ListArtists',
  components: { ListItemArtist, ModalDialogArtist },

  props: ['artists', 'media_kind'],

  data () {
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

<style>
</style>
