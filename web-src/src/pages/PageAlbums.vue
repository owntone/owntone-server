<template>
  <div>
    <tabs-music />
    <content-with-heading>
      <template #options>
        <index-button-list :indices="albums.indices" />
        <div class="columns">
          <div class="column">
            <p class="heading" v-text="$t('page.albums.filter')" />
            <div class="field">
              <control-switch v-model="uiStore.hide_singles">
                <template #label>
                  <span v-text="$t('page.albums.hide-singles')" />
                </template>
              </control-switch>
              <p class="help" v-text="$t('page.albums.hide-singles-help')" />
            </div>
            <div v-if="spotify_enabled" class="field">
              <control-switch v-model="uiStore.hide_spotify">
                <template #label>
                  <span v-text="$t('page.albums.hide-spotify')" />
                </template>
              </control-switch>
              <p class="help" v-text="$t('page.albums.hide-spotify-help')" />
            </div>
          </div>
          <div class="column">
            <p class="heading" v-text="$t('page.albums.sort.title')" />
            <control-dropdown
              v-model:value="uiStore.albums_sort"
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
      <template #content>
        <list-albums :items="albums" />
      </template>
    </content-with-heading>
  </div>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import ControlSwitch from '@/components/ControlSwitch.vue'
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
    ControlSwitch,
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
        (grouping) => grouping.id === this.uiStore.albums_sort
      )
      options.filters = [
        (album) => !this.uiStore.hide_singles || album.track_count > 2,
        (album) => !this.uiStore.hide_spotify || album.data_kind !== 'spotify'
      ]
      return this.albums_list.group(options)
    },
    spotify_enabled() {
      return this.servicesStore.spotify.webapi_token_valid
    }
  }
}
</script>
