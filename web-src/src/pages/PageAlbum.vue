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
      <list-tracks :items="tracks" :uris="album.uri" />
    </template>
  </content-with-hero>
  <modal-dialog-album
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
    name: 'music-artist',
    params: { id: album.value.artist_id }
  })
}

const openDetails = () => {
  showDetailsModal.value = true
}

const play = () => {
  queue.playUri(album.value.uri, true)
}

const heading = computed(() => ({
  count: t('data.tracks', { count: album.value.track_count }),
  handler: openArtist,
  subtitle: album.value.artist,
  title: album.value.name,
  actions: [
    { handler: play, icon: 'shuffle', key: 'actions.shuffle' },
    { handler: openDetails, icon: 'dots-horizontal' }
  ]
}))

onMounted(async () => {
  const [albumData, tracksData] = await Promise.all([
    library.album(route.params.id),
    library.albumTracks(route.params.id)
  ])
  album.value = albumData
  const grouped = new GroupedList(tracksData, {
    criteria: [{ field: 'disc_number', type: Number }],
    index: { field: 'disc_number', type: Number }
  })
  if (grouped.indices.length < 2) {
    grouped.group()
  }
  tracks.value = grouped
})
</script>
