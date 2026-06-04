<template>
  <tabs-audiobooks />
  <content-with-heading v-if="audiobooks.length">
    <template #heading>
      <pane-title
        :content="{ title: $t('page.spotify.audiobooks.saved-audiobooks') }"
      />
    </template>
    <template #content>
      <list-audiobooks-spotify :items="audiobooks" />
    </template>
    <template #footer>
      <router-link
        :to="{ name: 'audiobook-spotify-saved' }"
        class="button is-small is-rounded"
      >
        {{ $t('actions.show-more') }}
      </router-link>
    </template>
  </content-with-heading>
</template>

<script setup>
import { onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListAudiobooksSpotify from '@/components/ListAudiobooksSpotify.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import services from '@/api/services'

const PAGE_SIZE = 3
const audiobooks = ref([])

onMounted(async () => {
  try {
    const { api } = await services.spotify.get()
    const savedAudiobooks =
      await api.currentUser.audiobooks.savedAudiobooks(PAGE_SIZE)
    audiobooks.value = savedAudiobooks.items.map(
      (item) => item.audiobook ?? item
    )
  } catch {
    audiobooks.value = []
  }
})
</script>
