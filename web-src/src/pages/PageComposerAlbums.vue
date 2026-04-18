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
        :button="{ handler: play, icon: 'shuffle', key: 'actions.shuffle' }"
      />
    </template>
    <template #content>
      <list-albums :items="albums" />
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
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import ModalDialogComposer from '@/components/ModalDialogComposer.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import library from '@/api/library'
import queue from '@/api/queue'

const route = useRoute()
const router = useRouter()

const albums = ref(new GroupedList())
const composer = ref({})
const showDetailsModal = ref(false)

const expression = computed(
  () => `composer is "${composer.value.name}" and media_kind is music`
)

const openTracks = () => {
  router.push({
    name: 'music-composer-tracks',
    params: { name: composer.value.name }
  })
}

const heading = computed(() => {
  if (composer.value.name) {
    return {
      subtitle: [
        { count: composer.value.album_count, key: 'data.albums' },
        {
          count: composer.value.track_count,
          handler: openTracks,
          key: 'data.tracks'
        }
      ],
      title: composer.value.name
    }
  }
  return {}
})

const openDetails = () => {
  showDetailsModal.value = true
}

const play = () => {
  queue.playExpression(expression.value, true)
}

onMounted(async () => {
  const [composerData, albumData] = await Promise.all([
    library.composer(route.params.name),
    library.composerAlbums(route.params.name)
  ])
  composer.value = composerData
  albums.value = new GroupedList(albumData)
})
</script>
