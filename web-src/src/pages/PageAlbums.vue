<template>
  <div>
    <tabs-music></tabs-music>

    <template>
      <div class="container" v-if="links.length > 1">
        <div class="columns is-centered">
          <div class="column is-three-quarters">
            <div class="tabs is-centered is-small">
              <ul>
                <tab-idx-nav-item v-for="link in links" :key="link.n" :link="link"></tab-idx-nav-item>
              </ul>
            </div>
          </div>
        </div>
      </div>
    </template>

    <content-with-heading>
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
        <list-item-album v-for="album in dsp_albums" :key="album.id" :album="album" :links="links" v-if="!hide_singles || album.track_count > 2"></list-item-album>
        <infinite-loading v-if="dsp_total === 0 || dsp_offset < dsp_total" @infinite="dsp_next"><span slot="no-more">.</span></infinite-loading>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import { LoadDataBeforeEnterMixin } from './mixin'
import ContentWithHeading from '@/templates/ContentWithHeading'
import TabsMusic from '@/components/TabsMusic'
import ListItemAlbum from '@/components/ListItemAlbum'
import TabIdxNavItem from '@/components/TabsIdxNav'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'
import InfiniteLoading from 'vue-infinite-loading'

const albumsData = {
  load: function (to) {
    return webapi.library_albums()
  },

  set: function (vm, response) {
    vm.albums = response.data
    var li = 0
    var v = null
    var i
    for (i = 0; i < vm.albums.items.length; i++) {
      var n = vm.albums.items[i].name_sort.charAt(0).toUpperCase()
      if (n !== v) {
        var obj = {}
        obj.n = n
        obj.a = 'idx_nav_' + li
        vm.links.push(obj)
        li++
        v = n
      }
    }
    vm.dsp_total = vm.albums.items.length
  }
}

export default {
  name: 'PageAlbums',
  mixins: [ LoadDataBeforeEnterMixin(albumsData) ],
  components: { ContentWithHeading, TabsMusic, ListItemAlbum, TabIdxNavItem, InfiniteLoading },

  data () {
    return {
      albums: {},
      links: [],
      dsp_albums: [],
      dsp_offset: 0,
      dsp_total: 0
    }
  },

  computed: {
    hide_singles () {
      return this.$store.state.hide_singles
    }
  },

  methods: {
    update_hide_singles: function (e) {
      this.$store.commit(types.HIDE_SINGLES, !this.hide_singles)
    },

    dsp_next: function ($state) {
      this.dsp_albums = this.albums.items
      this.dsp_offset = this.dsp_albums.length

      if ($state) {
        $state.loaded()
        $state.complete()
      }
    }
  }
}
</script>

<style>
</style>
