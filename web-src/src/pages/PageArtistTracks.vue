<template>
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="tracks.indices" />
      <list-options>
        <template #filter>
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
            v-model:value="uiStore.artistTracksSort"
            :options="groupings"
          />
        </template>
      </list-options>
    </template>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #actions>
      <control-button
        :button="{ handler: openDetails, icon: 'dots-horizontal' }"
      />
      <control-button
        :button="{ handler: play, icon: 'shuffle', key: 'actions.shuffle' }"
      />
    </template>
    <template #content>
      <list-tracks :items="tracks" :uris="trackUris" />
    </template>
  </content-with-heading>
  <modal-dialog-artist
    :item="artist"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import ControlDropdown from '@/components/ControlDropdown.vue'
import ControlSwitch from '@/components/ControlSwitch.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import ListOptions from '@/components/ListOptions.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import library from '@/api/library'
import queue from '@/api/queue'
import { useI18n } from 'vue-i18n'
import { useServicesStore } from '@/stores/services'
import { useUIStore } from '@/stores/ui'

const uiStore = useUIStore()
const servicesStore = useServicesStore()
const route = useRoute()
const router = useRouter()
const { t } = useI18n()

const artist = ref({})
const showDetailsModal = ref(false)
const trackList = ref(new GroupedList())

const groupings = computed(() => [
  {
    id: 1,
    name: t('options.sort.name'),
    options: { index: { field: 'title_sort', type: String } }
  },
  {
    id: 2,
    name: t('options.sort.rating'),
    options: {
      criteria: [{ field: 'rating', order: -1, type: Number }],
      index: { field: 'rating', type: 'Digits' }
    }
  }
])

const tracks = computed(() => {
  const { options } = groupings.value.find(
    (grouping) => grouping.id === uiStore.artistTracksSort
  )
  options.filters = [
    (track) => !uiStore.hideSpotify || track.data_kind !== 'spotify'
  ]
  return trackList.value.group(options)
})

const albumCount = computed(
  () =>
    new Set(
      [...tracks.value]
        .filter((track) => track.isItem)
        .map((track) => track.item.album_id)
    ).size
)

const openArtist = () => {
  showDetailsModal.value = false
  router.push({
    name: 'music-artist',
    params: { id: artist.value.id }
  })
}

const heading = computed(() => ({
  subtitle: [
    { count: albumCount.value, handler: openArtist, key: 'data.albums' },
    { count: tracks.value.count, key: 'data.tracks' }
  ],
  title: artist.value.name
}))

const trackUris = computed(() =>
  trackList.value.items.map((item) => item.uri).join()
)

const openDetails = () => {
  showDetailsModal.value = true
}

const play = () => {
  queue.playUri(trackList.value.items.map((item) => item.uri).join(), true)
}

onMounted(async () => {
  const [artistData, tracksData] = await Promise.all([
    library.artist(route.params.id),
    library.artistTracks(route.params.id)
  ])
  artist.value = artistData
  trackList.value = new GroupedList(tracksData)
})
</script>
