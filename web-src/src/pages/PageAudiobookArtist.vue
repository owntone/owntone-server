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
        :button="{ handler: play, icon: 'play', key: 'actions.play' }"
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
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import ModalDialogArtist from '@/components/ModalDialogArtist.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import library from '@/api/library'
import queue from '@/api/queue'
import { useRoute } from 'vue-router'

const route = useRoute()

const albums = ref(new GroupedList())
const artist = ref({})
const showDetailsModal = ref(false)

const heading = computed(() => {
  if (artist.value.name) {
    return {
      subtitle: [{ count: artist.value.album_count, key: 'data.audiobooks' }],
      title: artist.value.name
    }
  }
  return {}
})

const openDetails = () => {
  showDetailsModal.value = true
}

const play = () => {
  queue.playUri(albums.value.items.map((item) => item.uri).join(), false)
}

onMounted(async () => {
  const [artistData, albumData] = await Promise.all([
    library.artist(route.params.id),
    library.artistAlbums(route.params.id)
  ])
  artist.value = artistData
  albums.value = new GroupedList(albumData)
})
</script>
