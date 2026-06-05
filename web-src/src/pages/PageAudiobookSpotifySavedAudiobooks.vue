<template>
  <tabs-audiobooks />
  <content-with-heading>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-audiobooks-spotify :items="audiobooks" :load="load" />
    </template>
  </content-with-heading>
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListAudiobooksSpotify from '@/components/ListAudiobooksSpotify.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import services from '@/api/services'
import { useI18n } from 'vue-i18n'

const PAGE_SIZE = 50

const { t } = useI18n()

const audiobooks = ref([])
const offset = ref(0)
const total = ref(0)

const heading = computed(() => ({
  subtitle: [{ count: total.value, key: 'data.audiobooks' }],
  title: t('page.spotify.audiobooks.saved-audiobooks')
}))

const appendAudiobooks = (data) => {
  const items = data.items.map((item) => item.audiobook ?? item)
  audiobooks.value = audiobooks.value.concat(items)
  total.value = data.total
  offset.value += data.limit
}

const load = async ({ loaded }) => {
  const { api } = await services.spotify.get()
  const data = await api.currentUser.audiobooks.savedAudiobooks(
    PAGE_SIZE,
    offset.value
  )
  appendAudiobooks(data)
  loaded(data.items.length, PAGE_SIZE)
}

onMounted(async () => {
  const { api } = await services.spotify.get()
  const data = await api.currentUser.audiobooks.savedAudiobooks(PAGE_SIZE, 0)
  appendAudiobooks(data)
})
</script>
