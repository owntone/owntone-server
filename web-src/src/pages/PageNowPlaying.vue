<template>
  <div
    class="is-flex is-align-items-center has-text-centered px-5 is-full-height"
  >
    <div v-if="track.id" class="mx-auto" style="max-width: 32rem">
      <control-image
        :url="track.artwork_url"
        :artist="track.artist"
        :album="track.album"
        class="is-clickable is-big"
        :class="{ 'is-masked': lyricsStore.pane }"
        @click="open_dialog(track)"
      />
      <lyrics-pane v-if="lyricsStore.pane" />
      <control-slider
        v-model:value="track_progress"
        class="mt-5"
        :disabled="is_live"
        :max="track_progress_max"
        @change="seek"
        @mousedown="start_dragging"
        @mouseup="end_dragging"
      />
      <div class="is-flex is-justify-content-space-between">
        <p class="subtitle is-7" v-text="track_elapsed_time" />
        <p class="subtitle is-7" v-text="track_total_time" />
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
        v-if="settingsStore.show_filepath_now_playing"
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
    :show="show_details_modal"
    :item="selected_item"
    @close="show_details_modal = false"
  />
</template>

<script>
import ControlImage from '@/components/ControlImage.vue'
import ControlSlider from '@/components/ControlSlider.vue'
import LyricsPane from '@/components/LyricsPane.vue'
import ModalDialogQueueItem from '@/components/ModalDialogQueueItem.vue'
import { useLyricsStore } from '@/stores/lyrics'
import { usePlayerStore } from '@/stores/player'
import { useQueueStore } from '@/stores/queue'
import { useSettingsStore } from '@/stores/settings'
import webapi from '@/webapi'

const INTERVAL = 1000

export default {
  name: 'PageNowPlaying',
  components: {
    ControlImage,
    ControlSlider,
    LyricsPane,
    ModalDialogQueueItem
  },
  setup() {
    return {
      lyricsStore: useLyricsStore(),
      playerStore: usePlayerStore(),
      queueStore: useQueueStore(),
      settingsStore: useSettingsStore()
    }
  },
  data() {
    return {
      INTERVAL,
      interval_id: 0,
      is_dragged: false,
      selected_item: {},
      show_details_modal: false
    }
  },
  computed: {
    composer() {
      if (this.settingsStore.show_composer_now_playing) {
        const genres = this.settingsStore.show_composer_for_genre
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
    is_live() {
      return this.track.length_ms === 0
    },
    track() {
      return this.queueStore.current
    },
    track_elapsed_time() {
      return this.$filters.toTimecode(this.track_progress * INTERVAL)
    },
    track_progress: {
      get() {
        return Math.floor(this.playerStore.item_progress_ms / INTERVAL)
      },
      set(value) {
        this.playerStore.item_progress_ms = value * INTERVAL
      }
    },
    track_progress_max() {
      return this.is_live ? 1 : Math.floor(this.track.length_ms / INTERVAL)
    },
    track_total_time() {
      return this.is_live
        ? this.$t('page.now-playing.live')
        : this.$filters.toTimecode(this.track.length_ms)
    }
  },
  watch: {
    'playerStore.state'(newState) {
      if (this.interval_id > 0) {
        window.clearTimeout(this.interval_id)
        this.interval_id = 0
      }
      if (newState === 'play') {
        this.interval_id = window.setInterval(this.tick, INTERVAL)
      }
    }
  },
  created() {
    webapi.player_status().then(({ data }) => {
      this.playerStore.$state = data
      if (this.playerStore.state === 'play') {
        this.interval_id = window.setInterval(this.tick, INTERVAL)
      }
    })
  },
  unmounted() {
    if (this.interval_id > 0) {
      window.clearTimeout(this.interval_id)
      this.interval_id = 0
    }
  },
  methods: {
    end_dragging() {
      this.is_dragged = false
    },
    open_dialog(item) {
      this.selected_item = item
      this.show_details_modal = true
    },
    seek() {
      if (!this.is_live) {
        webapi.player_seek_to_pos(this.track_progress * INTERVAL)
      }
    },
    start_dragging() {
      this.is_dragged = true
    },
    tick() {
      if (!this.is_dragged) {
        this.track_progress += 1
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
