<template>
  <div class="fd-page-with-tabs">
    <tabs-music />
    <content-with-heading>
      <template #options>
        <index-button-list :indices="albums.indices" />
        <div class="columns">
          <div class="column">
            <p class="heading mb-5" v-text="$t('page.albums.filter')" />
            <div class="field">
              <div class="control">
                <input
                  id="switchHideSingles"
                  v-model="hide_singles"
                  type="checkbox"
                  class="switch is-rounded"
                />
                <label
                  for="switchHideSingles"
                  v-text="$t('page.albums.hide-singles')"
                />
              </div>
              <p class="help" v-text="$t('page.albums.hide-singles-help')" />
            </div>
            <div v-if="spotify_enabled" class="field">
              <div class="control">
                <input
                  id="switchHideSpotify"
                  v-model="hide_spotify"
                  type="checkbox"
                  class="switch is-rounded"
                />
                <label
                  for="switchHideSpotify"
                  v-text="$t('page.albums.hide-spotify')"
                />
              </div>
              <p class="help" v-text="$t('page.albums.hide-spotify-help')" />
            </div>
          </div>
          <div class="column">
            <p class="heading mb-5" v-text="$t('page.albums.sort.title')" />
            <control-dropdown
              v-model:value="selected_grouping_option_id"
              :options="grouping_options"
            />
          </div>
        </div>
      </template>
      <template #heading-left>
        <p class="title is-4" v-text="$t('page.albums.title')" />
        <p
          class="heading"
          v-text="$t('page.albums.count', { count: albums.count })"
        />
      </template>
      <template #heading-right />
      <template #content>
        <list-albums :albums="albums" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import * as types from '@/store/mutation_types'
import { GroupedList, byName, byYear } from '@/lib/GroupedList'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return webapi.library_albums('music')
  },

  set(vm, response) {
    vm.albums_list = new GroupedList(response.data)
  }
}

export default {
  name: 'PageAlbums',
  components: {
    ContentWithHeading,
    ControlDropdown,
    IndexButtonList,
    ListAlbums,
    TabsMusic
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
      albums_list: new GroupedList(),
      grouping_options: [
        {
          id: 1,
          name: this.$t('page.albums.sort.name'),
          options: byName('name_sort', true)
        },
        {
          id: 2,
          name: this.$t('page.albums.sort.recently-added'),
          options: byYear('time_added', {
            direction: 'desc'
          })
        },
        {
          id: 3,
          name: this.$t('page.albums.sort.recently-released'),
          options: byYear('date_released', {
            direction: 'desc'
          })
        }
      ]
    }
  },

  computed: {
    albums() {
      const grouping = this.grouping_options.find(
        (o) => o.id === this.selected_grouping_option_id
      )
      this.albums_list.group(grouping.options, [
        (album) => !this.hide_singles || album.track_count > 2,
        (album) => !this.hide_spotify || album.data_kind !== 'spotify'
      ])

      return this.albums_list
    },

    selected_grouping_option_id: {
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
  }
}
</script>

<style></style>
