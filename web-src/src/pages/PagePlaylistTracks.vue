<template>
  <content-with-heading>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #actions>
      <control-button
        :button="{ handler: openDetails, icon: 'dots-horizontal' }"
      />
      <control-button
        :button="{
          handler: play,
          icon: 'shuffle',
          key: 'actions.shuffle'
        }"
        :disabled="tracks.count === 0"
      />
    </template>
    <template #content>
      <list-tracks :items="tracks" :uris="uris" />
    </template>
  </content-with-heading>
  <modal-dialog-playlist
    :item="playlist"
    :show="showDetailsModal"
    :uris="uris"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogPlaylist from '@/components/ModalDialogPlaylist.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import library from '@/api/library'
import queue from '@/api/queue'
import { useRoute } from 'vue-router'

const route = useRoute()

const playlist = ref({})
const showDetailsModal = ref(false)
const tracks = ref(new GroupedList())

const heading = computed(() => ({
  subtitle: [{ count: tracks.value.count, key: 'data.tracks' }],
  title: playlist.value.name
}))

const uris = computed(() => {
  if (playlist.value.random) {
    return tracks.value.map((item) => item.uri).join()
  }
  return playlist.value.uri
})

const play = () => {
  queue.playUri(uris.value, true)
}

const openDetails = () => {
  showDetailsModal.value = true
}

onMounted(async () => {
  const [playlistData, tracksData] = await Promise.all([
    library.playlist(route.params.id),
    library.playlistTracks(route.params.id)
  ])
  playlist.value = playlistData
  tracks.value = new GroupedList(tracksData)
})
</script>
