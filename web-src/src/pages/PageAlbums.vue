<template>
  <tabs-music />
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="albums.indices" />
      <list-options>
        <template #filter>
          <control-switch v-model="uiStore.hideSingles">
            <template #label>
              <span v-text="$t('options.filter.hide-singles')" />
            </template>
            <template #help>
              <span v-text="$t('options.filter.hide-singles-help')" />
            </template>
          </control-switch>
          <control-switch
            v-if="servicesStore.isSpotifyActive"
            v-model="uiStore.hideSpotify"
          >
            <template #label>
              <span v-text="$t('options.filter.hide-spotify')" />
            </template>
            <template #help>
              <span v-text="$t('options.filter.hide-spotify-help')" />
            </template>
          </control-switch>
        </template>
        <template #sort>
          <control-dropdown
            v-model:value="uiStore.albumsSort"
            :options="groupings"
          />
        </template>
      </list-options>
    </template>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-albums :items="albums" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import ControlSwitch from '@/components/ControlSwitch.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import ListOptions from '@/components/ListOptions.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import library from '@/api/library'
import { useServicesStore } from '@/stores/services'
import { useUIStore } from '@/stores/ui'

export default {
  name: 'PageAlbums',
  components: {
    ContentWithHeading,
    ControlDropdown,
    ControlSwitch,
    ListAlbums,
    ListIndexButtons,
    ListOptions,
    PaneTitle,
    TabsMusic
  },
  beforeRouteEnter(to, from, next) {
    library.albums('music').then((albums) => {
      next((vm) => {
        vm.albumList = new GroupedList(albums)
      })
    })
  },
  setup() {
    return { servicesStore: useServicesStore(), uiStore: useUIStore() }
  },
  data() {
    return {
      albumList: new GroupedList()
    }
  },
  computed: {
    albums() {
      const { options } = this.groupings.find(
        (grouping) => grouping.id === this.uiStore.albumsSort
      )
      options.filters = [
        (album) => !this.uiStore.hideSingles || album.track_count > 2,
        (album) => !this.uiStore.hideSpotify || album.data_kind !== 'spotify'
      ]
      return this.albumList.group(options)
    },
    groupings() {
      return [
        {
          id: 1,
          name: this.$t('options.sort.name'),
          options: { index: { field: 'name_sort', type: String } }
        },
        {
          id: 2,
          name: this.$t('options.sort.recently-added'),
          options: {
            criteria: [{ field: 'time_added', order: -1, type: Date }],
            index: { field: 'time_added', type: Date }
          }
        },
        {
          id: 3,
          name: this.$t('options.sort.recently-released'),
          options: {
            criteria: [{ field: 'date_released', order: -1, type: Date }],
            index: { field: 'date_released', type: Date }
          }
        },
        {
          id: 4,
          name: this.$t('options.sort.artist-name'),
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
          name: this.$t('options.sort.artist-date'),
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
        subtitle: [{ count: this.albums.count, key: 'data.albums' }],
        title: this.$t('page.albums.title')
      }
    }
  }
}
</script>
