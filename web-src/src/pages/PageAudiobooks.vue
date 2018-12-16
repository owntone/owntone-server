<template>
  <div>
    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Audiobooks</p>
        <p class="heading">{{ albums.total }} audiobooks</p>
      </template>
      <template slot="content">
        <list-item-album v-for="album in albums.items" :key="album.id" :album="album" :media_kind="'audiobook'" @click="open_album(album)">
          <template slot="actions">
            <a @click="open_dialog(album)">
              <span class="icon has-text-dark"><i class="mdi mdi-dots-vertical mdi-18px"></i></span>
            </a>
          </template>
        </list-item-album>
        <modal-dialog-album :show="show_details_modal" :album="selected_album" :media_kind="'audiobook'" @close="show_details_modal = false" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import ListItemAlbum from '@/components/ListItemAlbum'
import ModalDialogAlbum from '@/components/ModalDialogAlbum'
import webapi from '@/webapi'

const albumsData = {
  load: function (to) {
    return webapi.library_audiobooks()
  },

  set: function (vm, response) {
    vm.albums = response.data
  }
}

export default {
  name: 'PageAudiobooks',
  mixins: [ LoadDataBeforeEnterMixin(albumsData) ],
  components: { ContentWithHeading, ListItemAlbum, ModalDialogAlbum },

  data () {
    return {
      albums: {},

      show_details_modal: false,
      selected_album: {}
    }
  },

  methods: {
    open_album: function (album) {
      this.$router.push({ path: '/audiobooks/' + album.id })
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
