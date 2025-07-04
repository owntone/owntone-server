<template>
  <tabs-music />
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="artists.indices" />
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
            v-model:value="uiStore.artistsSort"
            :options="groupings"
          />
        </template>
      </list-options>
    </template>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-artists :items="artists" />
    </template>
  </content-with-heading>
</template>

<script>
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import ControlSwitch from '@/components/ControlSwitch.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListArtists from '@/components/ListArtists.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import ListOptions from '@/components/ListOptions.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import library from '@/api/library'
import { useServicesStore } from '@/stores/services'
import { useUIStore } from '@/stores/ui'

export default {
  name: 'PageArtists',
  components: {
    ContentWithHeading,
    ControlDropdown,
    ControlSwitch,
    ListArtists,
    ListIndexButtons,
    ListOptions,
    PaneTitle,
    TabsMusic
  },
  beforeRouteEnter(to, from, next) {
    library.artists('music').then((artists) => {
      next((vm) => {
        vm.artistList = new GroupedList(artists)
      })
    })
  },
  setup() {
    return { servicesStore: useServicesStore(), uiStore: useUIStore() }
  },
  data() {
    return {
      artistList: new GroupedList()
    }
  },
  computed: {
    artists() {
      const { options } = this.groupings.find(
        (grouping) => grouping.id === this.uiStore.artistsSort
      )
      options.filters = [
        (artist) =>
          !this.uiStore.hideSingles ||
          artist.track_count > artist.album_count * 2,
        (artist) => !this.uiStore.hideSpotify || artist.data_kind !== 'spotify'
      ]
      return this.artistList.group(options)
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
        }
      ]
    },
    heading() {
      return {
        subtitle: [{ count: this.artists.count, key: 'data.artists' }],
        title: this.$t('page.artists.title')
      }
    }
  }
}
</script>
