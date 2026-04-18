<template>
  <content-with-hero>
    <template #heading>
      <pane-hero :content="heading" />
    </template>
    <template #image>
      <control-image
        :url="album.images?.[0]?.url ?? ''"
        :caption="album.name"
        class="is-medium"
        @click="openDetails"
      />
    </template>
    <template #content>
      <list-tracks-spotify :items="tracks" :context-uri="album.uri" />
    </template>
  </content-with-hero>
  <modal-dialog-album-spotify
    :item="album"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import ContentWithHero from '@/templates/ContentWithHero.vue'
import ControlImage from '@/components/ControlImage.vue'
import ListTracksSpotify from '@/components/ListTracksSpotify.vue'
import ModalDialogAlbumSpotify from '@/components/ModalDialogAlbumSpotify.vue'
import PaneHero from '@/components/PaneHero.vue'
import queue from '@/api/queue'
import services from '@/api/services'
import { useI18n } from 'vue-i18n'

const route = useRoute()
const router = useRouter()
const { t } = useI18n()

const album = ref({ artists: [{}], tracks: {} })
const showDetailsModal = ref(false)

const openArtist = () => {
  router.push({
    name: 'music-spotify-artist',
    params: { id: album.value.artists[0].id }
  })
}

const openDetails = () => {
  showDetailsModal.value = true
}

const play = () => {
  showDetailsModal.value = false
  queue.playUri(album.value.uri, true)
}

const heading = computed(() => ({
  actions: [
    { handler: play, icon: 'shuffle', key: 'actions.shuffle' },
    { handler: openDetails, icon: 'dots-horizontal' }
  ],
  count: t('data.tracks', { count: album.value.tracks.total }),
  handler: openArtist,
  subtitle: album.value.artists?.[0]?.name,
  title: album.value.name
}))

const tracks = computed(() => {
  if (album.value.tracks?.total) {
    return album.value.tracks.items.map((track) => ({
      ...track,
      album: album.value
    }))
  }
  return []
})

onMounted(async () => {
  const { api, configuration } = await services.spotify.get()
  album.value = await api.albums.get(
    route.params.id,
    configuration.webapi_country
  )
})
</script>
