<template>
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="tracks.indices" />
      <list-options>
        <template #sort>
          <control-dropdown
            v-model:value="uiStore.composerTracksSort"
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
  <modal-dialog-composer
    :item="composer"
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
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import library from '@/api/library'
import queue from '@/api/queue'
import { useI18n } from 'vue-i18n'
import { useUIStore } from '@/stores/ui'

const route = useRoute()
const router = useRouter()
const { t } = useI18n()

const uiStore = useUIStore()

const composer = ref({})
const showDetailsModal = ref(false)
const trackList = ref(new GroupedList())

const expression = computed(
  () => `composer is "${composer.value.name}" and media_kind is music`
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

const openAlbums = () => {
  router.push({
    name: 'music-composer-albums',
    params: { name: composer.value.name }
  })
}

const heading = computed(() => {
  if (composer.value.name) {
    return {
      subtitle: [
        {
          count: composer.value.album_count,
          handler: openAlbums,
          key: 'data.albums'
        },
        { count: composer.value.track_count, key: 'data.tracks' }
      ],
      title: composer.value.name
    }
  }
  return {}
})

const tracks = computed(() => {
  const { options } = groupings.value.find(
    (grouping) => grouping.id === uiStore.composerTracksSort
  )

  return trackList.value.group(options)
})

const openDetails = () => {
  showDetailsModal.value = true
}

const play = () => {
  queue.playExpression(expression.value, true)
}

onMounted(async () => {
  const [composerData, tracksData] = await Promise.all([
    library.composer(route.params.name),
    library.composerTracks(route.params.name)
  ])
  composer.value = composerData
  trackList.value = new GroupedList(tracksData)
})
</script>
