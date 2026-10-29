<template>
  <content-with-heading>
    <template #heading>
      <pane-title :content="{ title: name }" />
    </template>
    <template #actions>
      <control-button
        :button="{ handler: openDetails, icon: 'dots-horizontal' }"
      />
      <control-button
        :button="{ handler: play, icon: 'play', key: 'actions.play' }"
      />
    </template>
    <template #content>
      <list-directories :items="directories" />
      <list-playlists :items="playlists" />
      <list-tracks :items="tracks" icon="file-music-outline" />
    </template>
  </content-with-heading>
  <modal-dialog-playable
    :item="playable"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import { computed, ref, watchEffect } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListDirectories from '@/components/ListDirectories.vue'
import ListPlaylists from '@/components/ListPlaylists.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogPlayable from '@/components/ModalDialogPlayable.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import library from '@/api/library'
import queue from '@/api/queue'
import { useConfigurationStore } from '@/stores/configuration'
import { useI18n } from 'vue-i18n'
import { useRoute } from 'vue-router'

const configurationStore = useConfigurationStore()
const route = useRoute()
const { t } = useI18n()

const directories = ref([])
const playlists = ref(new GroupedList())
const showDetailsModal = ref(false)
const tracks = ref(new GroupedList())

const current = computed(() => route.query?.directory || '/')

const expression = computed(
  () => `path starts with "${current.value.replace(/\/?$/u, '/')}" order by path asc`
)

const name = computed(() => {
  if (current.value !== '/') {
    return current.value?.slice(current.value.lastIndexOf('/') + 1)
  }
  return t('page.files.title')
})

const playable = computed(() => ({
  expression: expression.value,
  name: current.value,
  properties: [
    { key: 'property.folders', value: directories.value.length },
    { key: 'property.playlists', value: playlists.value.total },
    { key: 'property.tracks', value: tracks.value.total }
  ]
}))

const openDetails = () => {
  showDetailsModal.value = true
}

const play = () => {
  queue.playExpression(expression.value, false)
}

const transform = (path) => ({
  name: path.slice(path.lastIndexOf('/') + 1),
  path
})

const fetchData = async ({ directory }) => {
  if (directory) {
    const data = await library.files(directory)
    directories.value = data.directories.map((item) => transform(item.path))
    playlists.value = new GroupedList(data.playlists)
    tracks.value = new GroupedList(data.tracks)
  } else {
    directories.value = configurationStore.directories.map(transform)
    playlists.value = new GroupedList()
    tracks.value = new GroupedList()
  }
}

watchEffect(() => fetchData(route.query))
</script>
