<template>
  <div>
    <tabs-music />
    <content-with-heading>
      <template #options>
        <index-button-list :indices="artists.indices" />
        <div class="columns">
          <div class="column">
            <div
              class="is-size-7 is-uppercase"
              v-text="$t('page.artists.filter')"
            />
            <control-switch v-model="uiStore.hide_singles">
              <template #label>
                <span v-text="$t('page.artists.hide-singles')" />
              </template>
              <template #help>
                <span v-text="$t('page.artists.hide-singles-help')" />
              </template>
            </control-switch>
            <div v-if="spotify_enabled" class="field">
              <control-switch v-model="uiStore.hide_spotify">
                <template #label>
                  <span v-text="$t('page.artists.hide-spotify')" />
                </template>
                <template #help>
                  <span v-text="$t('page.artists.hide-spotify-help')" />
                </template>
              </control-switch>
            </div>
          </div>
          <div class="column">
            <div
              class="is-size-7 is-uppercase"
              v-text="$t('page.artists.sort.title')"
            />
            <control-dropdown
              v-model:value="uiStore.artists_sort"
              :options="groupings"
            />
          </div>
        </div>
      </template>
      <template #heading-left>
        <heading-title :content="heading" />
      </template>
      <template #content>
        <list-artists :items="artists" />
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
import ListArtists from '@/components/ListArtists.vue'
import TabsMusic from '@/components/TabsMusic.vue'
import { useServicesStore } from '@/stores/services'
import { useUIStore } from '@/stores/ui'
import webapi from '@/webapi'

const dataObject = {
  load(to) {
    return webapi.library_artists('music')
  },

  set(vm, response) {
    vm.artists_list = new GroupedList(response.data)
  }
}

export default {
  name: 'PageArtists',
  components: {
    ContentWithHeading,
    ControlDropdown,
    ControlSwitch,
    HeadingTitle,
    IndexButtonList,
    ListArtists,
    TabsMusic
  },
  beforeRouteEnter(to, from, next) {
    dataObject.load(to).then((response) => {
      next((vm) => dataObject.set(vm, response))
    })
  },
  setup() {
    return { servicesStore: useServicesStore(), uiStore: useUIStore() }
  },
  data() {
    return {
      artists_list: new GroupedList(),
      groupings: [
        {
          id: 1,
          name: this.$t('page.artists.sort.name'),
          options: { index: { field: 'name_sort', type: String } }
        },
        {
          id: 2,
          name: this.$t('page.artists.sort.recently-added'),
          options: {
            criteria: [{ field: 'time_added', order: -1, type: Date }],
            index: { field: 'time_added', type: Date }
          }
        }
      ]
    }
  },
  computed: {
    artists() {
      const { options } = this.groupings.find(
        (grouping) => grouping.id === this.uiStore.artists_sort
      )
      options.filters = [
        (artist) =>
          !this.uiStore.hide_singles ||
          artist.track_count > artist.album_count * 2,
        (artist) => !this.uiStore.hide_spotify || artist.data_kind !== 'spotify'
      ]
      return this.artists_list.group(options)
    },
    heading() {
      return {
        title: this.$t('page.artists.title'),
        subtitle: [{ key: 'count.artists', count: this.artists.count }]
      }
    },
    spotify_enabled() {
      return this.servicesStore.spotify.webapi_token_valid
    }
  }
}
</script>
