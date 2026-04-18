<template>
  <content-with-heading>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #actions>
      <control-button
        v-for="button in buttons"
        :key="button.key"
        :button="button"
      />
    </template>
    <template #content>
      <list-albums-spotify v-if="albums.length" :items="albums" :load="load" />
    </template>
  </content-with-heading>
  <modal-dialog-artist-spotify
    :item="artist"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import ListAlbumsSpotify from '@/components/ListAlbumsSpotify.vue'
import ModalDialogArtistSpotify from '@/components/ModalDialogArtistSpotify.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import queue from '@/api/queue'
import services from '@/api/services'
import { useI18n } from 'vue-i18n'
import { useRoute } from 'vue-router'

const PAGE_SIZE = 50

const route = useRoute()
const { t } = useI18n()

const albums = ref([])
const artist = ref({})
const offset = ref(0)
const showDetailsModal = ref(false)
const total = ref(0)

const openDetails = () => {
  showDetailsModal.value = true
}

const play = () => {
  showDetailsModal.value = false
  queue.playUri(artist.value.uri, true)
}

const playTopTracks = async () => {
  const { api, configuration } = await services.spotify.get()
  const tracks = await api.artists.topTracks(
    artist.value.id,
    configuration.webapi_country
  )
  const uris = tracks.tracks.map((item) => item.uri).join(',')
  queue.playUri(uris, false)
}

const buttons = computed(() => [
  { handler: openDetails, icon: 'dots-horizontal' },
  {
    handler: playTopTracks,
    icon: 'play',
    key: t('actions.play-top-tracks')
  },
  { handler: play, icon: 'shuffle', key: 'actions.shuffle' }
])

const heading = computed(() => ({
  subtitle: [{ count: total.value, key: 'data.albums' }],
  title: artist.value.name
}))

const appendAlbums = (data) => {
  albums.value = albums.value.concat(data.items)
  total.value = data.total
  offset.value += data.limit
}

const load = async ({ loaded }) => {
  const { api, configuration } = await services.spotify.get()
  const albumsRes = await api.artists.albums(
    artist.value.id,
    'album,single',
    configuration.webapi_country,
    PAGE_SIZE,
    offset.value
  )
  appendAlbums(albumsRes)
  loaded(albumsRes.items.length, PAGE_SIZE)
}

onMounted(async () => {
  const { api, configuration } = await services.spotify.get()
  const [artistData, albumsData] = await Promise.all([
    api.artists.get(route.params.id),
    api.artists.albums(
      route.params.id,
      'album,single',
      configuration.webapi_country,
      PAGE_SIZE,
      0
    )
  ])
  artist.value = artistData
  appendAlbums(albumsData)
})
</script>
