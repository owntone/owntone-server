<template>
  <div>
    <tabs-music />
    <content-with-heading>
      <template #options>
        <index-button-list :indices="albums.indices" />
        <div class="columns">
          <div class="column">
            <div
              class="is-size-7 is-uppercase"
              v-text="$t('page.albums.filter.title')"
            />
            <control-switch v-model="uiStore.hide_singles">
              <template #label>
                <span v-text="$t('page.albums.filter.hide-singles')" />
              </template>
              <template #help>
                <span v-text="$t('page.albums.filter.hide-singles-help')" />
              </template>
            </control-switch>
            <control-switch
              v-if="spotify_enabled"
              v-model="uiStore.hide_spotify"
            >
              <template #label>
                <span v-text="$t('page.albums.filter.hide-spotify')" />
              </template>
              <template #help>
                <span v-text="$t('page.albums.filter.hide-spotify-help')" />
              </template>
            </control-switch>
          </div>
          <div class="column">
            <div
              class="is-size-7 is-uppercase"
              v-text="$t('page.albums.sort.title')"
            />
            <control-dropdown
              v-model:value="uiStore.albums_sort"
              :options="groupings"
            />
          </div>
        </div>
      </template>
      <template #heading-left>
        <heading-title :content="heading" />
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
import HeadingTitle from '@/components/HeadingTitle.vue'
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
    HeadingTitle,
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
      albums_list: new GroupedList()
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
    groupings() {
      return [
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
    },
    heading() {
      return {
        title: this.$t('page.albums.title'),
        subtitle: [{ key: 'count.albums', count: this.albums.count }]
      }
    },
    spotify_enabled() {
      return this.servicesStore.spotify.webapi_token_valid
    }
  }
}
</script>
