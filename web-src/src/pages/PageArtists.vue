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
              v-text="$t('options.filter.title')"
            />
            <control-switch v-model="uiStore.hideSingles">
              <template #label>
                <span v-text="$t('options.filter.hide-singles')" />
              </template>
              <template #help>
                <span v-text="$t('options.filter.hide-singles-help')" />
              </template>
            </control-switch>
            <div v-if="servicesStore.isSpotifyEnabled" class="field">
              <control-switch v-model="uiStore.hideSpotify">
                <template #label>
                  <span v-text="$t('options.filter.hide-spotify')" />
                </template>
                <template #help>
                  <span v-text="$t('options.filter.hide-spotify-help')" />
                </template>
              </control-switch>
            </div>
          </div>
          <div class="column">
            <div
              class="is-size-7 is-uppercase"
              v-text="$t('options.sort.title')"
            />
            <control-dropdown
              v-model:value="uiStore.artistsSort"
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
    vm.artistList = new GroupedList(response.data)
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
        subtitle: [{ count: this.artists.count, key: 'count.artists' }],
        title: this.$t('page.artists.title')
      }
    }
  }
}
</script>
