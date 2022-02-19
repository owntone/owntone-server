<template>
  <div class="fd-page-with-tabs">
    <tabs-music></tabs-music>

    <content-with-heading>
      <template v-slot:options>
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
      <template v-slot:heading-left>
        <p class="title is-4">Albums</p>
        <p class="heading">{{ albums_list.sortedAndFiltered.length }} Albums</p>
      </template>
      <template v-slot:heading-right>
      </template>
      <template v-slot:content>
        <list-albums :albums="albums_list"></list-albums>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import DropdownMenu from '@/components/DropdownMenu.vue'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'
import Albums from '@/lib/Albums'

const dataObject = {
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
  },

  beforeRouteEnter (to, from, next) {
    dataObject.load(to).then((response) => {
      next(vm => dataObject.set(vm, response))
    })
  },
  beforeRouteUpdate (to, from, next) {
    if (this.albums.items.length  > 0) {
      next()
      return
    }
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  }
}
</script>

<style>
</style>
