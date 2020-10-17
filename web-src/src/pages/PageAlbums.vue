<template>
  <div>
    <tabs-music></tabs-music>

    <content-with-heading>
      <template slot="options">
        <index-button-list :index="albums_list.indexList"></index-button-list>

        <div class="columns">
          <div class="column">
            <p class="heading" style="margin-bottom: 24px;">Filter</p>
            <div class="field">
              <div class="control">
                <input id="switchHideSingles" type="checkbox" name="switchHideSingles" class="switch" v-model="hide_singles">
                <label for="switchHideSingles">Hide singles</label>
              </div>
              <p class="help">If active, hides singles and albums with tracks that only appear in playlists.</p>
            </div>
            <div class="field" v-if="spotify_enabled">
              <div class="control">
                <input id="switchHideSpotify" type="checkbox" name="switchHideSpotify" class="switch" v-model="hide_spotify">
                <label for="switchHideSpotify">Hide albums from Spotify</label>
              </div>
              <p class="help">If active, hides albums that only appear in your Spotify library.</p>
            </div>
          </div>
          <div class="column">
            <p class="heading" style="margin-bottom: 24px;">Sort by</p>
            <dropdown-menu v-model="sort" :options="sort_options"></dropdown-menu>
          </div>
        </div>
      </template>
      <template slot="heading-left">
        <p class="title is-4">Albums</p>
        <p class="heading">{{ albums_list.sortedAndFiltered.length }} Albums</p>
      </template>
      <template slot="heading-right">
      </template>
      <template slot="content">
        <list-albums :albums="albums_list"></list-albums>
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
import DropdownMenu from '@/components/DropdownMenu'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'
import Albums from '@/lib/Albums'

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
  components: { ContentWithHeading, TabsMusic, IndexButtonList, ListAlbums, DropdownMenu },

  data () {
    return {
      albums: { items: [] },
      sort_options: ['Name', 'Recently added', 'Recently released']
    }
  },

  computed: {
    albums_list () {
      return new Albums(this.albums.items, {
        hideSingles: this.hide_singles,
        hideSpotify: this.hide_spotify,
        sort: this.sort,
        group: true
      })
    },

    spotify_enabled () {
      return this.$store.state.spotify.webapi_token_valid
    },

    hide_singles: {
      get () {
        return this.$store.state.hide_singles
      },
      set (value) {
        this.$store.commit(types.HIDE_SINGLES, value)
      }
    },

    hide_spotify: {
      get () {
        return this.$store.state.hide_spotify
      },
      set (value) {
        this.$store.commit(types.HIDE_SPOTIFY, value)
      }
    },

    sort: {
      get () {
        return this.$store.state.albums_sort
      },
      set (value) {
        this.$store.commit(types.ALBUMS_SORT, value)
      }
    }
  },

  methods: {
    scrollToTop: function () {
      window.scrollTo({ top: 0, behavior: 'smooth' })
    }
  }
}
</script>

<style>
</style>
