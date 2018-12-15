<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="options">
        <index-button-list :index="index_list"></index-button-list>
      </template>
      <template slot="heading-left">
        <p class="title is-4">Albums</p>
        <p class="heading">{{ albums.total }} albums</p>
      </template>
      <template slot="heading-right">
        <a class="button is-small" :class="{ 'is-info': hide_singles }" @click="update_hide_singles">
          <span class="icon">
            <i class="mdi mdi-numeric-1-box-multiple-outline"></i>
          </span>
          <span>Hide singles</span>
        </a>
      </template>
      <template slot="content">
        <list-item-album v-for="(album, index) in albums.items" :key="album.id" :album="album" :anchor="anchor(album, index)" v-if="!hide_singles || album.track_count > 2">
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
import IndexButtonList from '@/components/IndexButtonList'
import ListItemAlbum from '@/components/ListItemAlbum'
import ModalDialogAlbum from '@/components/ModalDialogAlbum'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'

const albumsData = {
  load: function (to) {
    return webapi.library_albums()
  },

  set: function (vm, response) {
    vm.albums = response.data
  }
}

export default {
  name: 'PageAlbums',
  mixins: [ LoadDataBeforeEnterMixin(albumsData) ],
  components: { ContentWithHeading, TabsMusic, IndexButtonList, ListItemAlbum, ModalDialogAlbum },

  data () {
    return {
      albums: { items: [] },

      show_details_modal: false,
      selected_album: {}
    }
  },

  computed: {
    hide_singles () {
      return this.$store.state.hide_singles
    },

    index_list () {
      return [...new Set(this.albums.items
        .filter(album => !this.$store.state.hide_singles || album.track_count > 2)
        .map(album => album.name_sort.charAt(0).toUpperCase()))]
    }
  },

  methods: {
    update_hide_singles: function (e) {
      this.$store.commit(types.HIDE_SINGLES, !this.hide_singles)
    },

    anchor: function (album, index) {
      return album.name_sort.charAt(0).toUpperCase()
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
