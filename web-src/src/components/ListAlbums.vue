<template>
  <div>
    <list-item-album v-for="album in albums"
        :key="album.id"
        :album="album"
        @click="open_album(album)">
        <template slot="artwork" v-if="is_visible_artwork">
            <p class="image is-64x64 fd-has-shadow fd-has-action">
            <cover-artwork
                :artwork_url="album.artwork_url"
                :artist="album.artist"
                :album="album.name"
                :maxwidth="64"
                :maxheight="64" />
            </p>
        </template>
        <template slot="actions">
            <a @click="open_dialog(album)">
            <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
        </template>
    </list-item-album>
    <modal-dialog-album :show="show_details_modal" :album="selected_album" :media_kind="media_kind" @close="show_details_modal = false" />
  </div>
</template>

<script>
import ListItemAlbum from '@/components/ListItemAlbum'
import ModalDialogAlbum from '@/components/ModalDialogAlbum'
import CoverArtwork from '@/components/CoverArtwork'

export default {
  name: 'ListAlbums',
  components: { ListItemAlbum, ModalDialogAlbum, CoverArtwork },

  props: ['albums', 'media_kind'],

  data () {
    return {
      show_details_modal: false,
      selected_album: {}
    }
  },

  computed: {
    is_visible_artwork () {
      return this.$store.getters.settings_option('webinterface', 'show_cover_artwork_in_album_lists').value
    },

    media_kind_resolved: function () {
      return this.media_kind ? this.media_kind : this.selected_album.media_kind
    }
  },

  methods: {
    open_album: function (album) {
      this.selected_album = album
      if (this.media_kind_resolved === 'podcast') {
        this.$router.push({ path: '/podcasts/' + album.id })
      } else if (this.media_kind_resolved === 'audiobook') {
        this.$router.push({ path: '/audiobooks/' + album.id })
      } else {
        this.$router.push({ path: '/music/albums/' + album.id })
      }
    },

    open_dialog: function (album) {
      this.selected_album = album
      this.show_details_modal = true
    }
  }
}
</script>

<style>
</style>
