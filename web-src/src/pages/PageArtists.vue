<template>
  <div class="fd-page-with-tabs">
    <tabs-music></tabs-music>

    <content-with-heading>
      <template v-slot:options>
        <index-button-list :index="artists_list.indexList"></index-button-list>

        <div class="columns">
          <div class="column">
            <p class="heading" style="margin-bottom: 24px;">Filter</p>
            <div class="field">
              <div class="control">
                <input id="switchHideSingles" type="checkbox" name="switchHideSingles" class="switch" v-model="hide_singles">
                <label for="switchHideSingles">Hide singles</label>
              </div>
              <p class="help">If active, hides artists that only appear on singles or playlists.</p>
            </div>
            <div class="field" v-if="spotify_enabled">
              <div class="control">
                <input id="switchHideSpotify" type="checkbox" name="switchHideSpotify" class="switch" v-model="hide_spotify">
                <label for="switchHideSpotify">Hide artists from Spotify</label>
              </div>
              <p class="help">If active, hides artists that only appear in your Spotify library.</p>
            </div>
          </div>
          <div class="column">
            <p class="heading" style="margin-bottom: 24px;">Sort by</p>
            <dropdown-menu v-model="sort" :options="sort_options"></dropdown-menu>
          </div>
        </div>
      </template>
      <template v-slot:heading-left>
        <p class="title is-4">Artists</p>
        <p class="heading">{{ artists_list.sortedAndFiltered.length }} Artists</p>
      </template>
      <template v-slot:heading-right>
      </template>
      <template v-slot:content>
        <list-artists :artists="artists_list"></list-artists>
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListArtists from '@/components/ListArtists.vue'
import DropdownMenu from '@/components/DropdownMenu.vue'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'
import Artists from '@/lib/Artists'

const dataObject = {
  load: function (to) {
    return webapi.library_artists('music')
  },

  set: function (vm, response) {
    vm.artists = response.data
  }
}

export default {
  name: 'PageArtists',
  components: { ContentWithHeading, TabsMusic, IndexButtonList, ListArtists, DropdownMenu },

  data () {
    return {
      artists: { items: [] },
      sort_options: ['Name', 'Recently added']
    }
  },

  computed: {
    artists_list () {
      return new Artists(this.artists.items, {
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
        return this.$store.state.artists_sort
      },
      set (value) {
        this.$store.commit(types.ARTISTS_SORT, value)
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
    if (this.artists.items.length  > 0) {
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
