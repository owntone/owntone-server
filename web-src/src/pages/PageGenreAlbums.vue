<template>
  <content-with-heading>
    <template #options>
      <list-index-buttons :indices="albums.indices" />
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
      <list-albums :items="albums" />
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
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import ListIndexButtons from '@/components/ListIndexButtons.vue'
import ModalDialogGenre from '@/components/ModalDialogGenre.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import library from '@/api/library'
import queue from '@/api/queue'

const route = useRoute()
const router = useRouter()

const albums = ref(new GroupedList())
const genre = ref({})
const mediaKind = ref(route.query.mediaKind)
const showDetailsModal = ref(false)

const openDetails = () => {
  showDetailsModal.value = true
}

const openTracks = () => {
  showDetailsModal.value = false
  router.push({
    name: 'genre-tracks',
    params: { name: genre.value.name },
    query: { mediaKind: mediaKind.value }
  })
}

const play = () => {
  queue.playExpression(
    `genre is "${genre.value.name}" and media_kind is ${mediaKind.value}`,
    true
  )
}

const heading = computed(() => {
  if (genre.value?.name) {
    return {
      subtitle: [
        { count: genre.value.album_count, key: 'data.albums' },
        {
          count: genre.value.track_count,
          handler: openTracks,
          key: 'data.tracks'
        }
      ],
      title: genre.value.name
    }
  }
  return {}
})

const loadData = async () => {
  const [genreData, albumsData] = await Promise.all([
    library.genre(route.params.name, route.query.mediaKind),
    library.genreAlbums(route.params.name, route.query.mediaKind)
  ])
  genre.value = genreData.items.shift()
  albums.value = new GroupedList(albumsData, {
    index: { field: 'name_sort', type: String }
  })
}

onMounted(loadData)
</script>
