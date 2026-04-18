<template>
  <content-with-heading>
    <template #options>
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
            v-model:value="uiStore.artistAlbumsSort"
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
      <list-albums :items="albums" />
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
import ListAlbums from '@/components/ListAlbums.vue'
import ListOptions from '@/components/ListOptions.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import library from '@/api/library'
import queue from '@/api/queue'
import { useI18n } from 'vue-i18n'
import { useServicesStore } from '@/stores/services'
import { useUIStore } from '@/stores/ui'

const route = useRoute()
const router = useRouter()
const { t } = useI18n()

const servicesStore = useServicesStore()
const uiStore = useUIStore()

const albumList = ref(new GroupedList())
const artist = ref({})
const showDetailsModal = ref(false)

const groupings = computed(() => [
  {
    id: 1,
    name: t('options.sort.name'),
    options: { criteria: [{ field: 'name_sort', type: String }] }
  },
  {
    id: 2,
    name: t('options.sort.release-date'),
    options: { criteria: [{ field: 'date_released', type: Date }] }
  }
])

const albums = computed(() => {
  const { options } = groupings.value.find(
    (grouping) => grouping.id === uiStore.artistAlbumsSort
  )
  options.filters = [
    (album) => !uiStore.hideSpotify || album.data_kind !== 'spotify'
  ]
  return albumList.value.group(options)
})

const openTracks = () => {
  router.push({ name: 'music-artist-tracks', params: { id: artist.value.id } })
}

const trackCount = computed(() =>
  [...albums.value].reduce(
    (total, album) => total + (album?.item.track_count || 0),
    0
  )
)

const heading = computed(() => ({
  subtitle: [
    { count: albums.value.count, key: 'data.albums' },
    {
      count: trackCount.value,
      handler: openTracks,
      key: 'data.tracks'
    }
  ],
  title: artist.value.name
}))

const openDetails = () => {
  showDetailsModal.value = true
}

const play = () => {
  queue.playUri(albums.value.items.map((item) => item.uri).join(), true)
}

onMounted(async () => {
  const [artistData, albumData] = await Promise.all([
    library.artist(route.params.id),
    library.artistAlbums(route.params.id)
  ])
  artist.value = artistData
  albumList.value = new GroupedList(albumData)
})
</script>
