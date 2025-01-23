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
              v-model:value="selected_grouping_id"
              :options="groupings"
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
        <list-albums :items="albums" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import { GroupedList } from '@/lib/GroupedList'
import IndexButtonList from '@/components/IndexButtonList.vue'
import ListAlbums from '@/components/ListAlbums.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import { useServicesStore } from '@/stores/services'
import { useUIStore } from '@/stores/ui'
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

  setup() {
    return { uiStore: useUIStore(), servicesStore: useServicesStore() }
  },

  data() {
    return {
      albums_list: new GroupedList(),
      groupings: [
        {
          id: 1,
          name: this.$t('page.albums.sort.name'),
          options: { index: { field: 'name_sort', type: String } }
        },
        {
          id: 2,
          name: this.$t('page.albums.sort.recently-added'),
          options: {
            criteria: [{ field: 'time_added', order: -1, type: Date }],
            index: { field: 'time_added', type: Date }
          }
        },
        {
          id: 3,
          name: this.$t('page.albums.sort.recently-released'),
          options: {
            criteria: [{ field: 'date_released', order: -1, type: Date }],
            index: { field: 'date_released', type: Date }
          }
        },
        {
          id: 4,
          name: this.$t('page.albums.sort.artist-name'),
          options: {
            criteria: [
              { field: 'artist', type: String },
              { field: 'name_sort', type: String }
            ],
            index: { field: 'artist', type: String }
          }
        },
        {
          id: 5,
          name: this.$t('page.albums.sort.artist-date'),
          options: {
            criteria: [
              { field: 'artist', type: String },
              { field: 'date_released', type: Date }
            ],
            index: { field: 'artist', type: String }
          }
        }
      ]
    }
  },

  computed: {
    albums() {
      const { options } = this.groupings.find(
        (grouping) => grouping.id === this.selected_grouping_id
      )
      options.filters = [
        (album) => !this.hide_singles || album.track_count > 2,
        (album) => !this.hide_spotify || album.data_kind !== 'spotify'
      ]
      return this.albums_list.group(options)
    },
    hide_singles: {
      get() {
        return this.uiStore.hide_singles
      },
      set(value) {
        this.uiStore.hide_singles = value
      }
    },
    hide_spotify: {
      get() {
        return this.uiStore.hide_spotify
      },
      set(value) {
        this.uiStore.hide_spotify = value
      }
    },
    selected_grouping_id: {
      get() {
        return this.uiStore.albums_sort
      },
      set(value) {
        this.uiStore.albums_sort = value
      }
    },
    spotify_enabled() {
      return this.servicesStore.spotify.webapi_token_valid
    }
  }
}
</script>

<style></style>
