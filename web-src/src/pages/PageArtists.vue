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

<script setup>
import { computed, onMounted, ref } from 'vue'

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
import { useI18n } from 'vue-i18n'
import { useServicesStore } from '@/stores/services'
import { useUIStore } from '@/stores/ui'

const servicesStore = useServicesStore()
const uiStore = useUIStore()

const { t } = useI18n()

const artistList = ref(new GroupedList())

const groupings = computed(() => [
  {
    id: 1,
    name: t('options.sort.name'),
    options: { index: { field: 'name_sort', type: String } }
  },
  {
    id: 2,
    name: t('options.sort.recently-added'),
    options: {
      criteria: [{ field: 'time_added', order: -1, type: Date }],
      index: { field: 'time_added', type: Date }
    }
  }
])

const artists = computed(() => {
  const grouping = groupings.value.find((g) => g.id === uiStore.artistsSort)
  const options = { ...grouping.options }
  options.filters = [
    (artist) =>
      !uiStore.hideSingles || artist.track_count > artist.album_count * 2,
    (artist) => !uiStore.hideSpotify || artist.data_kind !== 'spotify'
  ]
  return artistList.value.group(options)
})

const heading = computed(() => ({
  subtitle: [{ count: artists.value.count, key: 'data.artists' }],
  title: t('page.artists.title')
}))

onMounted(async () => {
  const data = await library.artists('music')
  artistList.value = new GroupedList(data)
})
</script>
