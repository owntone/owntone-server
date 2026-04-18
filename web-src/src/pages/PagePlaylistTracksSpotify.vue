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
        :disabled="playlist.tracks.total === 0"
      />
    </template>
    <template #content>
      <list-tracks-spotify
        v-if="tracks.length"
        :context-uri="playlist.uri"
        :items="tracks"
        :load="load"
      />
    </template>
  </content-with-heading>
  <modal-dialog-playlist-spotify
    :item="playlist"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import ListTracksSpotify from '@/components/ListTracksSpotify.vue'
import ModalDialogPlaylistSpotify from '@/components/ModalDialogPlaylistSpotify.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import queue from '@/api/queue'
import services from '@/api/services'
import { useRoute } from 'vue-router'

const PAGE_SIZE = 50

const route = useRoute()

const offset = ref(0)
const playlist = ref({ tracks: {} })
const showDetailsModal = ref(false)
const total = ref(0)
const tracks = ref([])

const heading = computed(() => {
  if (playlist.value.name) {
    return {
      subtitle: [{ count: playlist.value.tracks?.total, key: 'data.tracks' }],
      title: playlist.value.name
    }
  }
  return {}
})

const appendTracks = (data) => {
  let position = Math.max(
    -1,
    ...tracks.value.map((item) => item.position).filter(Boolean)
  )
  data.items.forEach((item) => {
    const { track } = item
    if (track) {
      if (track.is_playable) {
        position += 1
        track.position = position
      }
      tracks.value.push(track)
    }
  })
  total.value = data.total
  offset.value += data.limit
}

const load = async ({ loaded }) => {
  const { api, configuration } = await services.spotify.get()
  const data = await api.playlists.getPlaylistItems(
    playlist.value.id,
    configuration.webapi_country,
    null,
    PAGE_SIZE,
    offset.value
  )
  appendTracks(data)
  loaded(data.items.length, PAGE_SIZE)
}

const play = () => {
  showDetailsModal.value = false
  queue.playUri(playlist.value.uri, true)
}

const openDetails = () => {
  showDetailsModal.value = true
}

onMounted(async () => {
  const { api, configuration } = await services.spotify.get()
  const [playlistData, tracksData] = await Promise.all([
    api.playlists.getPlaylist(route.params.id),
    api.playlists.getPlaylistItems(
      route.params.id,
      configuration.webapi_country,
      null,
      PAGE_SIZE,
      0
    )
  ])
  playlist.value = playlistData
  appendTracks(tracksData)
})
</script>
