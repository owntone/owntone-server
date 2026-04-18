<template>
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="tracks.indices" />
      <list-options>
        <template #sort>
          <control-dropdown
            v-model:value="uiStore.genreTracksSort"
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
      <list-tracks :items="tracks" :expression="expression" />
    </template>
  </content-with-heading>
  <modal-dialog-genre
    :item="genre"
    :media-kind="mediaKind"
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
import { GroupedList } from '@/lib/GroupedList'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import ListOptions from '@/components/ListOptions.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogGenre from '@/components/ModalDialogGenre.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import library from '@/api/library'
import queue from '@/api/queue'
import { useI18n } from 'vue-i18n'
import { useUIStore } from '@/stores/ui'

defineOptions({ name: 'PageGenreTracks' })

const route = useRoute()
const router = useRouter()
const { t } = useI18n()
const uiStore = useUIStore()

const genre = ref({})
const mediaKind = ref(route.query.mediaKind)
const showDetailsModal = ref(false)
const trackList = ref(new GroupedList())

const expression = computed(
  () => `genre is "${genre.value.name}" and media_kind is ${mediaKind.value}`
)

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

const openGenre = () => {
  showDetailsModal.value = false
  router.push({
    name: 'genre-albums',
    params: { name: genre.value.name },
    query: { mediaKind: mediaKind.value }
  })
}

const heading = computed(() => {
  if (genre.value.name) {
    return {
      subtitle: [
        {
          count: genre.value.album_count,
          handler: openGenre,
          key: 'data.albums'
        },
        { count: genre.value.track_count, key: 'data.tracks' }
      ],
      title: genre.value.name
    }
  }
  return {}
})

const tracks = computed(() => {
  const grouping =
    groupings.value.find((g) => g.id === uiStore.genreTracksSort) ??
    groupings.value[0]
  return trackList.value.group(grouping.options)
})

const openDetails = () => {
  showDetailsModal.value = true
}

const play = () => {
  queue.playExpression(expression.value, true)
}

onMounted(async () => {
  const [genreData, tracksData] = await Promise.all([
    library.genre(route.params.name, route.query.mediaKind),
    library.genreTracks(route.params.name, route.query.mediaKind)
  ])
  genre.value = genreData.items.shift()
  trackList.value = new GroupedList(tracksData)
})
</script>
