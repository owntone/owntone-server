<template>
  <content-with-hero>
    <template #heading>
      <pane-hero :content="heading" />
    </template>
    <template #image>
      <control-image
        :url="album.artwork_url"
        :caption="album.name"
        class="is-medium"
        @click="openDetails"
      />
    </template>
    <template #content>
      <list-tracks :items="tracks" :show-progress="true" :uris="album.uri" />
    </template>
  </content-with-hero>
  <modal-dialog-album
    :item="album"
    :show="showDetailsModal"
    media-kind="audiobook"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import { useRoute, useRouter } from 'vue-router'
import ContentWithHero from '@/templates/ContentWithHero.vue'
import ControlImage from '@/components/ControlImage.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogAlbum from '@/components/ModalDialogAlbum.vue'
import PaneHero from '@/components/PaneHero.vue'
import library from '@/api/library'
import queue from '@/api/queue'
import { useI18n } from 'vue-i18n'

const route = useRoute()
const router = useRouter()
const { t } = useI18n()

const album = ref({})
const showDetailsModal = ref(false)
const tracks = ref(new GroupedList())

const openArtist = () => {
  showDetailsModal.value = false
  router.push({
    name: 'audiobook-artist',
    params: { id: album.value.artist_id }
  })
}

const openDetails = () => {
  showDetailsModal.value = true
}

const play = () => {
  queue.playUri(album.value.uri, false)
}

const heading = computed(() => ({
  actions: [
    { handler: play, icon: 'play', key: 'actions.play' },
    { handler: openDetails, icon: 'dots-horizontal' }
  ],
  count: t('data.tracks', { count: album.value.track_count }),
  handler: openArtist,
  subtitle: album.value.artist,
  title: album.value.name
}))

onMounted(async () => {
  const [albumData, trackData] = await Promise.all([
    library.album(route.params.id),
    library.albumTracks(route.params.id)
  ])
  album.value = albumData
  tracks.value = new GroupedList(trackData)
})
</script>
