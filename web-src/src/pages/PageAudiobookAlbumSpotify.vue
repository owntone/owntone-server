<template>
  <content-with-hero>
    <template #heading>
      <pane-hero :content="heading" />
    </template>
    <template #image>
      <control-image
        :url="audiobook.images?.[0]?.url ?? ''"
        :caption="audiobook.name"
        class="is-medium"
        @click="openDetails"
      />
    </template>
    <template #content>
      <list-chapters-spotify
        :items="chapters"
        :context-uri="audiobook.uri"
      />
    </template>
  </content-with-hero>
  <modal-dialog-audiobook-spotify
    :item="audiobook"
    :show="showDetailsModal"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import { computed, onMounted, ref } from 'vue'
import ContentWithHero from '@/templates/ContentWithHero.vue'
import ControlImage from '@/components/ControlImage.vue'
import ListChaptersSpotify from '@/components/ListChaptersSpotify.vue'
import ModalDialogAudiobookSpotify from '@/components/ModalDialogAudiobookSpotify.vue'
import PaneHero from '@/components/PaneHero.vue'
import queue from '@/api/queue'
import services from '@/api/services'
import { useI18n } from 'vue-i18n'
import { useRoute } from 'vue-router'

const route = useRoute()
const { t } = useI18n()

const audiobook = ref({ authors: [{}], chapters: {} })
const showDetailsModal = ref(false)

const openDetails = () => {
  showDetailsModal.value = true
}

const play = () => {
  showDetailsModal.value = false
  queue.playUri(audiobook.value.uri, false)
}

const heading = computed(() => ({
  actions: [
    { handler: play, icon: 'play', key: 'actions.play' },
    { handler: openDetails, icon: 'dots-horizontal' }
  ],
  count: t('data.chapters', {
    count: audiobook.value.chapters?.total ?? 0
  }),
  subtitle: audiobook.value.authors?.[0]?.name,
  title: audiobook.value.name
}))

const chapters = computed(() => {
  if (audiobook.value.chapters?.total) {
    return audiobook.value.chapters.items
  }
  return []
})

onMounted(async () => {
  const { api, configuration } = await services.spotify.get()
  audiobook.value = await api.audiobooks.get(
    route.params.id,
    configuration.webapi_country
  )
})
</script>

