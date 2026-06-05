<template>
  <tabs-audiobooks />
  <content-with-heading>
    <template #heading>
      <pane-title :content="heading" />
    </template>
    <template #content>
      <list-audiobooks-spotify :items="audiobooks" />
    </template>
    <template v-if="audiobooks.length" #footer>
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
import { computed, onMounted, ref } from 'vue'
import ContentWithHeading from '@/templates/ContentWithHeading.vue'
import ListAudiobooksSpotify from '@/components/ListAudiobooksSpotify.vue'
import PaneTitle from '@/components/PaneTitle.vue'
import TabsAudiobooks from '@/components/TabsAudiobooks.vue'
import services from '@/api/services'
import { useI18n } from 'vue-i18n'

const PAGE_SIZE = 3
const audiobooks = ref([])

const { t } = useI18n()

const heading = computed(() => ({
  subtitle: [{ count: audiobooks.value.length, key: 'data.audiobooks' }],
  title: t('page.spotify.audiobooks.saved-audiobooks')
}))

onMounted(async () => {
  const { api } = await services.spotify.get()
  const savedAudiobooks =
    await api.currentUser.audiobooks.savedAudiobooks(PAGE_SIZE)
  audiobooks.value = savedAudiobooks.items.map((item) => item.audiobook ?? item)
})
</script>
