<template>
  <div
    class="is-flex is-align-items-center has-text-centered px-5 is-full-height"
  >
    <div v-if="track.id" class="mx-auto" style="max-width: 32rem">
      <div class="is-relative">
        <control-image
          :url="track.artwork_url"
          :caption="track.album"
          class="is-clickable is-big"
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
      <p class="title is-5" v-text="$t('page.now-playing.title')" />
      <p class="subtitle" v-text="$t('page.now-playing.info')" />
    </div>
  </div>
  <modal-dialog-queue-item
    :show="showDetailsModal"
    :item="selectedItem"
    @close="showDetailsModal = false"
  />
</template>

<script>
import ControlImage from '@/components/ControlImage.vue'
import ControlSlider from '@/components/ControlSlider.vue'
import ModalDialogQueueItem from '@/components/ModalDialogQueueItem.vue'
import PaneLyrics from '@/components/PaneLyrics.vue'
import player from '@/api/player'
import { usePlayerStore } from '@/stores/player'
import { useQueueStore } from '@/stores/queue'
import { useSettingsStore } from '@/stores/settings'

const INTERVAL = 1000

export default {
  name: 'PageNowPlaying',
  components: {
    ControlImage,
    ControlSlider,
    PaneLyrics,
    ModalDialogQueueItem
  },
  setup() {
    return {
      playerStore: usePlayerStore(),
      queueStore: useQueueStore(),
      settingsStore: useSettingsStore()
    }
  },
  data() {
    return {
      INTERVAL,
      intervalId: 0,
      isDragged: false,
      selectedItem: {},
      showDetailsModal: false
    }
  },
  computed: {
    composer() {
      if (this.settingsStore.showComposerNowPlaying) {
        const genres = this.settingsStore.showComposerForGenre
        if (
          !genres ||
          (this.track.genre &&
            genres
              .toLowerCase()
              .split(',')
              .findIndex(
                (elem) =>
                  this.track.genre.toLowerCase().indexOf(elem.trim()) >= 0
              ) >= 0)
        ) {
          return this.track.composer
        }
      }
      return null
    },
    isLive() {
      return this.track.length_ms === 0
    },
    track() {
      return this.queueStore.current
    },
    trackElapsedTime() {
      return this.$formatters.toTimecode(this.trackProgress * INTERVAL)
    },
    trackProgress: {
      get() {
        return Math.floor(this.playerStore.item_progress_ms / INTERVAL)
      },
      set(value) {
        this.playerStore.item_progress_ms = value * INTERVAL
      }
    },
    trackProgressMax() {
      return this.isLive ? 1 : Math.floor(this.track.length_ms / INTERVAL)
    },
    trackTotalTime() {
      return this.$t('page.now-playing.time', this.track.length_ms, {
        named: {
          time: this.$formatters.toTimecode(this.track.length_ms)
        }
      })
    }
  },
  watch: {
    'playerStore.state'(newState) {
      if (this.intervalId > 0) {
        window.clearTimeout(this.intervalId)
        this.intervalId = 0
      }
      if (newState === 'play') {
        this.intervalId = window.setInterval(this.tick, INTERVAL)
      }
    }
  },
  created() {
    if (this.playerStore.state === 'play') {
      this.intervalId = window.setInterval(this.tick, INTERVAL)
    }
  },
  unmounted() {
    if (this.intervalId > 0) {
      window.clearTimeout(this.intervalId)
      this.intervalId = 0
    }
  },
  methods: {
    endDragging() {
      this.isDragged = false
    },
    openDetails(item) {
      this.selectedItem = item
      this.showDetailsModal = true
    },
    seek() {
      if (!this.isLive) {
        player.seekToPosition(this.trackProgress * INTERVAL)
      }
    },
    startDragging() {
      this.isDragged = true
    },
    tick() {
      if (!this.isDragged) {
        this.trackProgress += 1
      }
    }
  }
}
</script>

<style scoped>
.is-full-height {
  min-height: calc(100vh - calc(2 * var(--bulma-navbar-height)));
}
</style>
