<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="heading-left">
        <p class="title is-4">Recently added</p>
        <p class="heading">albums</p>
      </template>
      <template slot="content">
        <list-item-album v-for="album in recently_added.items" :key="album.id" :album="album" @click="open_album(album)">
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
        <modal-dialog-album :show="show_details_modal" :album="selected_album" @close="show_details_modal = false" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import ListItemAlbum from '@/components/ListItemAlbum'
import ModalDialogAlbum from '@/components/ModalDialogAlbum'
import CoverArtwork from '@/components/CoverArtwork'
import webapi from '@/webapi'

const browseData = {
  load: function (to) {
    return webapi.search({
      type: 'album',
      expression: 'time_added after 8 weeks ago and media_kind is music having track_count > 3 order by time_added desc',
      limit: 50
    })
  },

  set: function (vm, response) {
    vm.recently_added = response.data.albums
  }
}

export default {
  name: 'PageBrowseType',
  mixins: [LoadDataBeforeEnterMixin(browseData)],
  components: { ContentWithHeading, TabsMusic, ListItemAlbum, ModalDialogAlbum, CoverArtwork },

  data () {
    return {
      recently_added: {},

      show_details_modal: false,
      selected_album: {}
    }
  },

  computed: {
    is_visible_artwork () {
      return this.$store.getters.settings_option('webinterface', 'show_cover_artwork_in_album_lists').value
    }
  },

  methods: {
    open_album: function (album) {
      this.$router.push({ path: '/music/albums/' + album.id })
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
