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

<script setup>
import { computed, onMounted, ref } from 'vue'
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
import { useI18n } from 'vue-i18n'
import { useServicesStore } from '@/stores/services'
import { useUIStore } from '@/stores/ui'

const { t } = useI18n()
const servicesStore = useServicesStore()
const uiStore = useUIStore()

const albumList = ref(new GroupedList())

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
  },
  {
    id: 3,
    name: t('options.sort.recently-released'),
    options: {
      criteria: [{ field: 'date_released', order: -1, type: Date }],
      index: { field: 'date_released', type: Date }
    }
  },
  {
    id: 4,
    name: t('options.sort.artist-name'),
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
    name: t('options.sort.artist-date'),
    options: {
      criteria: [
        { field: 'artist', type: String },
        { field: 'date_released', type: Date }
      ],
      index: { field: 'artist', type: String }
    }
  }
])

const albums = computed(() => {
  const selected =
    groupings.value.find((grouping) => grouping.id === uiStore.albumsSort) ??
    groupings.value[0]
  const options = {
    ...selected.options,
    filters: [
      (album) => !uiStore.hideSingles || album.track_count > 2,
      (album) => !uiStore.hideSpotify || album.data_kind !== 'spotify'
    ]
  }
  return albumList.value.group(options)
})

const heading = computed(() => ({
  subtitle: [{ count: albums.value.count, key: 'data.albums' }],
  title: t('page.albums.title')
}))

onMounted(async () => {
  const albumsData = await library.albums('music')
  albumList.value = new GroupedList(albumsData)
})
</script>
