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
        <list-item-album v-for="(album, index) in albums.items" :key="album.id" :album="album" :anchor="anchor(album, index)" v-if="!hide_singles || album.track_count > 2"></list-item-album>
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
  components: { ContentWithHeading, TabsMusic, IndexButtonList, ListItemAlbum },

  data () {
    return {
      albums: { items: [] }
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
    }
  }
}
</script>

<style>
</style>
