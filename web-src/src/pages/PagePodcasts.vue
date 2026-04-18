<template>
  <content-with-heading v-if="episodes.items.length > 0">
    <template #heading>
      <pane-title :content="{ title: $t('page.podcasts.new-episodes') }" />
    </template>
    <template #actions>
      <control-button
        :button="{
          handler: markAllAsPlayed,
          icon: 'pencil',
          key: 'actions.mark-all-played'
        }"
      />
    </template>
    <template #content>
      <list-tracks
        :items="episodes"
        :show-progress="true"
        @play-count-changed="reloadEpisodes"
      />
    </template>
  </content-with-heading>
  <content-with-heading>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #actions>
      <control-button
        v-if="hasRss"
        :button="{
          handler: updateRss,
          icon: 'refresh',
          key: 'actions.update'
        }"
      />
      <control-button
        :button="{
          handler: openAddPodcastDialog,
          icon: 'rss',
          key: 'actions.add'
        }"
      />
    </template>
    <template #content>
      <list-albums
        :items="albums"
        @play-count-changed="reloadEpisodes"
        @podcast-deleted="reloadPodcasts"
      />
    </template>
  </content-with-heading>
  <modal-dialog-add-rss
    :show="showAddPodcastModal"
    @close="showAddPodcastModal = false"
    @podcast-added="reloadPodcasts"
  />
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ControlButton from '@/components/ControlButton.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListAlbums from '@/components/ListAlbums.vue'
import ListTracks from '@/components/ListTracks.vue'
import ModalDialogAddRss from '@/components/ModalDialogAddRss.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import library from '@/api/library'
import { useI18n } from 'vue-i18n'
import { useLibraryStore } from '@/stores/library'
import { useUIStore } from '@/stores/ui'

const { t } = useI18n()

const libraryStore = useLibraryStore()
const uiStore = useUIStore()

const albums = ref([])
const episodes = ref({ items: [] })
const rssCount = ref({})
const showAddPodcastModal = ref(false)

const hasRss = computed(() => (rssCount.value.albums ?? 0) > 0)

const heading = computed(() => {
  if (albums.value.total) {
    return {
      subtitle: [{ count: albums.value.count, key: 'data.podcasts' }],
      title: t('page.podcasts.title')
    }
  }
  return {}
})

const markAllAsPlayed = () => {
  episodes.value.items.forEach((episode) => {
    library.updateTrack(episode.id, { play_count: 'increment' })
  })
  episodes.value.items = {}
}

const openAddPodcastDialog = () => {
  showAddPodcastModal.value = true
}

const reloadEpisodes = async () => {
  const e = await library.newPodcastEpisodes()
  episodes.value = new GroupedList(e)
}

const reloadRssCount = async () => {
  const r = await library.rssCount()
  rssCount.value = r
}

const reloadPodcasts = async () => {
  const a = await library.albums('podcast')
  albums.value = new GroupedList(a)
  await reloadEpisodes()
  await reloadRssCount()
}

const updateRss = () => {
  libraryStore.update_dialog_scan_kind = 'rss'
  uiStore.showUpdateDialog = true
}

onMounted(async () => {
  const [albumsData, episodesData, rssCountData] = await Promise.all([
    library.albums('podcast'),
    library.newPodcastEpisodes(),
    library.rssCount()
  ])
  albums.value = new GroupedList(albumsData)
  episodes.value = new GroupedList(episodesData)
  rssCount.value = rssCountData
})
</script>
