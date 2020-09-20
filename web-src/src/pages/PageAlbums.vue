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
        <list-albums :albums="albums_filtered"></list-albums>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import IndexButtonList from '@/components/IndexButtonList'
import ListAlbums from '@/components/ListAlbums'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'

const albumsData = {
  load: function (to) {
    return webapi.library_albums('music')
  },

  set: function (vm, response) {
    vm.albums = response.data
    vm.index_list = [...new Set(vm.albums.items
      .filter(album => !vm.$store.state.hide_singles || album.track_count > 2)
      .map(album => album.name_sort.charAt(0).toUpperCase()))]
  }
}

export default {
  name: 'PageAlbums',
  mixins: [LoadDataBeforeEnterMixin(albumsData)],
  components: { ContentWithHeading, TabsMusic, IndexButtonList, ListAlbums },

  data () {
    return {
      albums: { items: [] },
      index_list: []
    }
  },

  computed: {
    hide_singles () {
      return this.$store.state.hide_singles
    },

    albums_filtered () {
      if (this.hide_singles) {
        return this.albums.items.filter(album => album.track_count > 2)
      }
      return this.albums.items
    }
  },

  methods: {
    update_hide_singles: function (e) {
      this.$store.commit(types.HIDE_SINGLES, !this.hide_singles)
    }
  },

  watch: {
    'hide_singles' () {
      this.index_list = [...new Set(this.albums.items
        .filter(album => !this.$store.state.hide_singles || album.track_count > 2)
        .map(album => album.name_sort.charAt(0).toUpperCase()))]
    }
  }
}
</script>

<style>
</style>
