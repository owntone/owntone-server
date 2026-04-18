<template>
  <div
    class="is-flex is-align-items-center has-text-centered px-5 is-full-height"
  >
    <div v-if="!queueStore.isEmpty" class="mx-auto" style="max-width: 32rem">
      <div class="is-relative">
        <control-image
          :url="track.artwork_url"
          :caption="track.album"
          class="is-big"
          :class="{ 'is-masked': playerStore.showLyrics }"
          @click="openDetails(track)"
        />
        <pane-lyrics v-if="playerStore.showLyrics" />
      </div>
      <control-slider
        v-model:value="trackProgress"
        class="mt-5"
        :disabled="isLive"
        :max="trackProgressMax"
        @change="seek"
        @mousedown="startDragging"
        @mouseup="endDragging"
      />
      <div class="is-flex is-justify-content-space-between">
        <p class="subtitle is-7" v-text="trackElapsedTime" />
        <p class="subtitle is-7" v-text="trackTotalTime" />
      </div>
      <p class="title is-5" v-text="track.title" />
      <p class="title is-6" v-text="track.artist" />
      <p
        v-if="composer"
        class="subtitle is-6 has-text-grey has-text-weight-bold"
        v-text="composer"
      />
      <p v-if="track.album" class="subtitle is-6" v-text="track.album" />
      <p
        v-if="settingsStore.showFilepathNowPlaying"
        class="subtitle is-6 has-text-grey"
        v-text="track.path"
      />
    </div>
    <div v-else class="mx-auto">
      <p class="title is-5" v-text="$t('page.player.title')" />
      <p class="subtitle" v-text="$t('page.player.info')" />
    </div>
  </div>
  <modal-dialog-queue-item
    :show="showDetailsModal"
    :item="selectedItem"
    @close="showDetailsModal = false"
  />
</template>

<script setup>
import { computed, onMounted, onUnmounted, ref, watch } from 'vue'
import ControlImage from '@/components/ControlImage.vue'
import ControlSlider from '@/components/ControlSlider.vue'
import ModalDialogQueueItem from '@/components/ModalDialogQueueItem.vue'
import PaneLyrics from '@/components/PaneLyrics.vue'
import formatters from '@/lib/Formatters'
import player from '@/api/player'
import { useI18n } from 'vue-i18n'
import { usePlayerStore } from '@/stores/player'
import { useQueueStore } from '@/stores/queue'
import { useSettingsStore } from '@/stores/settings'

const INTERVAL = 1000

const playerStore = usePlayerStore()
const queueStore = useQueueStore()
const settingsStore = useSettingsStore()
const { t } = useI18n()

const intervalId = ref(0)
const isDragged = ref(false)
const selectedItem = ref({})
const showDetailsModal = ref(false)

const track = computed(() => queueStore.current)
const isLive = computed(() => track.value.length_ms === 0)
const composer = computed(() => {
  if (settingsStore.showComposerNowPlaying) {
    const genres = settingsStore.showComposerForGenre
    if (
      !genres ||
      (track.value.genre &&
        genres
          .toLowerCase()
          .split(',')
          .findIndex(
            (elem) => track.value.genre.toLowerCase().indexOf(elem.trim()) >= 0
          ) >= 0)
    ) {
      return track.value.composer
    }
  }
  return null
})
const trackProgress = computed({
  get() {
    return Math.floor(playerStore.item_progress_ms / INTERVAL)
  },
  set(value) {
    playerStore.item_progress_ms = value * INTERVAL
  }
})
const trackProgressMax = computed(
  () => Number(isLive.value) || Math.floor(track.value.length_ms / INTERVAL)
)
const trackElapsedTime = computed(() =>
  formatters.toTimecode(trackProgress.value * INTERVAL)
)
const trackTotalTime = computed(() =>
  t('page.player.time', track.value.length_ms, {
    named: {
      time: formatters.toTimecode(track.value.length_ms)
    }
  })
)

const startDragging = () => {
  isDragged.value = true
}

const endDragging = () => {
  isDragged.value = false
}

const openDetails = (item) => {
  selectedItem.value = item
  showDetailsModal.value = true
}

const seek = () => {
  if (!isLive.value) {
    player.seekToPosition(trackProgress.value * INTERVAL)
  }
}

const tick = () => {
  if (!isDragged.value) {
    trackProgress.value += 1
  }
}

const clearTimer = () => {
  if (intervalId.value > 0) {
    window.clearInterval(intervalId.value)
    intervalId.value = 0
  }
}

watch(
  () => playerStore.state,
  (state) => {
    clearTimer()
    if (state === 'play') {
      intervalId.value = window.setInterval(tick, INTERVAL)
    }
  }
)

onMounted(() => {
  if (playerStore.state === 'play') {
    intervalId.value = window.setInterval(tick, INTERVAL)
  }
})

onUnmounted(clearTimer)
</script>

<style scoped>
.is-full-height {
  min-height: calc(100vh - calc(2 * var(--bulma-navbar-height)));
}
</style>
