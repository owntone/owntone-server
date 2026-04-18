<template>
  <content-with-heading>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-playlists :items="playlists" />
    </template>
  </content-with-heading>
</template>

<script setup>
import { computed, onMounted, ref, watch } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import { GroupedList } from '@/lib/GroupedList'
import ListPlaylists from '@/components/ListPlaylists.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import library from '@/api/library'
import { useConfigurationStore } from '@/stores/configuration'
import { useI18n } from 'vue-i18n'
import { useRoute } from 'vue-router'

const route = useRoute()
const { t } = useI18n()

const configurationStore = useConfigurationStore()

const playlist = ref({})
const playlistList = ref(new GroupedList())

const playlists = computed(() =>
  playlistList.value.group({
    filters: [
      (item) =>
        item.folder ||
        configurationStore.radio_playlists ||
        item.stream_count === 0 ||
        item.item_count > item.stream_count
    ]
  })
)

const heading = computed(() => ({
  subtitle: [{ count: playlists.value.count, key: 'data.playlists' }],
  title: t('page.playlists.title', playlists.value.count, {
    name: playlist.value.name
  })
}))

const fetchData = async (id) => {
  const [playlistData, playlistFolderData] = await Promise.all([
    library.playlist(id),
    library.playlistFolder(id)
  ])
  playlist.value = playlistData
  playlistList.value = new GroupedList(playlistFolderData)
}

watch(
  () => route.params.id,
  (id) => {
    fetchData(id)
  }
)

onMounted(() => {
  fetchData(route.params.id)
})
</script>
