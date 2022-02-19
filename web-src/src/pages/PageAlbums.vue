<template>
  <div class="fd-page-with-tabs">
    <tabs-music />

    <content-with-heading>
      <template #options>
        <index-button-list :index="albums.indexList" />

        <div class="columns">
          <div class="column">
            <p class="heading" style="margin-bottom: 24px">Filter</p>
            <div class="field">
              <div class="control">
                <input
                  id="switchHideSingles"
                  v-model="hide_singles"
                  type="checkbox"
                  name="switchHideSingles"
                  class="switch"
                />
                <label for="switchHideSingles">Hide singles</label>
              </div>
              <p class="help">
                If active, hides singles and albums with tracks that only appear
                in playlists.
              </p>
            </div>
            <div v-if="spotify_enabled" class="field">
              <div class="control">
                <input
                  id="switchHideSpotify"
                  v-model="hide_spotify"
                  type="checkbox"
                  name="switchHideSpotify"
                  class="switch"
                />
                <label for="switchHideSpotify">Hide albums from Spotify</label>
              </div>
              <p class="help">
                If active, hides albums that only appear in your Spotify
                library.
              </p>
            </div>
          </div>
          <div class="column">
            <p class="heading" style="margin-bottom: 24px">Sort by</p>
            <dropdown-menu
              v-model="selected_groupby_option_name"
              :options="groupby_option_names"
            />
          </div>
        </div>
      </template>
      <template #heading-left>
        <p class="title is-4">Albums</p>
        <p class="heading">{{ albums.count }} Albums</p>
      </template>
      <template #heading-right />
      <template #content>
        <list-albums :albums="albums" />
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
import { bySortName, byYear, GroupByList } from '@/lib/GroupByList'

const dataObject = {
  load: function (to) {
    return webapi.library_albums('music')
  },

  set: function (vm, response) {
    vm.albums_list = new GroupByList(response.data)
  }
}

export default {
  name: 'PageAlbums',
  components: {
    ContentWithHeading,
    TabsMusic,
    IndexButtonList,
    ListAlbums,
    DropdownMenu
  },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },

  beforeRouteUpdate(to, from, next) {
    if (!this.albums_list.isEmpty()) {
      next()
      return
    }
    const vm = this
    dataObject.load(to).then((response) => {
      dataObject.set(vm, response)
      next()
    })
  },

  data() {
    return {
      albums_list: new GroupByList(),

      // List of group by/sort options for itemsGroupByList
      groupby_options: [
        { name: 'Name', options: bySortName('name_sort') },
        {
          name: 'Recently added',
          options: byYear('time_added', {
            direction: 'desc',
            defaultValue: '0000'
          })
        },
        {
          name: 'Recently released',
          options: byYear('date_released', {
            direction: 'desc',
            defaultValue: '0000'
          })
        }
      ]
    }
  },

  computed: {
    albums() {
      const groupBy = this.groupby_options.find(
        (o) => o.name === this.selected_groupby_option_name
      )
      this.albums_list.group(groupBy.options, [
        (album) => !this.hide_singles || album.track_count <= 2,
        (album) => !this.hide_spotify || album.data_kind !== 'spotify'
      ])

      return this.albums_list
    },

    groupby_option_names() {
      return [...this.groupby_options].map((o) => o.name)
    },

    selected_groupby_option_name: {
      get() {
        return this.$store.state.albums_sort
      },
      set(value) {
        this.$store.commit(types.ALBUMS_SORT, value)
      }
    },

    spotify_enabled() {
      return this.$store.state.spotify.webapi_token_valid
    },

    hide_singles: {
      get() {
        return this.$store.state.hide_singles
      },
      set(value) {
        this.$store.commit(types.HIDE_SINGLES, value)
      }
    },

    hide_spotify: {
      get() {
        return this.$store.state.hide_spotify
      },
      set(value) {
        this.$store.commit(types.HIDE_SPOTIFY, value)
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

<style></style>
