<template>
  <div class="fd-page-with-tabs">
    <tabs-music />

    <content-with-heading>
      <template #options>
        <index-button-list :index="artists.indexList" />

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
                If active, hides artists that only appear on singles or
                playlists.
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
                <label for="switchHideSpotify">Hide artists from Spotify</label>
              </div>
              <p class="help">
                If active, hides artists that only appear in your Spotify
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
        <p class="title is-4">Artists</p>
        <p class="heading">{{ artists.count }} Artists</p>
      </template>
      <template #heading-right />
      <template #content>
        <list-artists :artists="artists" />
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
import { bySortName, byYear, GroupByList } from '@/lib/GroupByList'

const dataObject = {
  load: function (to) {
    return webapi.library_artists('music')
  },

  set: function (vm, response) {
    vm.artists_list = new GroupByList(response.data)
  }
}

export default {
  name: 'PageArtists',
  components: {
    ContentWithHeading,
    TabsMusic,
    IndexButtonList,
    ListArtists,
    DropdownMenu
  },

  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },

  beforeRouteUpdate(to, from, next) {
    if (!this.artists_list.isEmpty()) {
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
      // Original data from API call
      artists_list: new GroupByList(),

      // List of group by/sort options for itemsGroupByList
      groupby_options: [
        { name: 'Name', options: bySortName('name_sort') },
        {
          name: 'Recently added',
          options: byYear('time_added', {
            direction: 'desc',
            defaultValue: '0000'
          })
        }
      ]
    }
  },

  computed: {
    // Wraps GroupByList and updates it if filter or sort changes
    artists() {
      if (!this.artists_list) {
        return []
      }

      const groupBy = this.groupby_options.find(
        (o) => o.name === this.selected_groupby_option_name
      )
      this.artists_list.group(groupBy.options, [
        (artist) =>
          !this.hide_singles || artist.track_count <= artist.album_count * 2,
        (artist) => !this.hide_spotify || artist.data_kind !== 'spotify'
      ])

      return this.artists_list
    },

    // List for the drop down menu
    groupby_option_names() {
      return [...this.groupby_options].map((o) => o.name)
    },

    selected_groupby_option_name: {
      get() {
        return this.$store.state.artists_sort
      },
      set(value) {
        this.$store.commit(types.ARTISTS_SORT, value)
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

  methods: {}
}
</script>

<style></style>
