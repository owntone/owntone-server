<template>
  <div class="hero is-fullheight">
    <div v-if="track.id > 0" class="hero-body">
      <div class="container has-text-centered" style="max-width: 500px">
        <cover-artwork
          :artwork_url="track.artwork_url"
          :artist="track.artist"
          :album="track.album"
          class="is-clickable fd-has-shadow fd-cover-big-image"
          @click="open_dialog(track)"
        />
        <input
          v-model.number="item_progress_ms"
          :step="1000"
          :max="is_live ? 1000 : track.length_ms"
          type="range"
          class="slider mt-5"
          :style="{ '--ratio': progress }"
          @change="seek"
          @touchstart="start_dragging"
          @touchend="end_dragging"
        />
        <div class="is-flex is-justify-content-space-between">
          <p
            class="subtitle is-7"
            v-text="$filters.durationInHours(item_progress_ms)"
          />
          <p
            class="subtitle is-7"
            v-text="$filters.durationInHours(track.length_ms)"
          />
        </div>
        <h1 class="title is-5" v-text="track.title" />
        <h2 class="title is-6" v-text="track.artist" />
        <h2
          v-if="composer"
          class="subtitle is-6 has-text-grey has-text-weight-bold"
          v-text="composer"
        />
        <h3 class="subtitle is-6" v-text="track.album" />
        <h3
          v-if="filepath"
          class="subtitle is-6 has-text-grey"
          v-text="filepath"
        />
      </div>
    </div>
    <div v-else class="hero-body">
      <div class="container has-text-centered">
        <p class="title is-5" v-text="$t('page.now-playing.title')" />
        <p class="subtitle" v-text="$t('page.now-playing.info')" />
      </div>
    </div>
    <modal-dialog-queue-item
      :show="show_details_modal"
      :item="selected_item"
      @close="show_details_modal = false"
    />
  </div>
</template>

<script>
import ModalDialogQueueItem from '@/components/ModalDialogQueueItem.vue'
import CoverArtwork from '@/components/CoverArtwork.vue'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'

export default {
  name: 'PageNowPlaying',
  components: {
    ModalDialogQueueItem,
    CoverArtwork
  },

  data() {
    return {
      item_progress_ms: 0,
      interval_id: 0,
      is_dragged: false,

      show_details_modal: false,
      selected_item: {}
    }
  },

  computed: {
    progress() {
      return this.is_live ? 2 : this.item_progress_ms / this.track.length_ms
    },

    is_live() {
      return this.track.length_ms == 0
    },

    player() {
      return this.$store.state.player
    },

    track() {
      return this.$store.getters.now_playing
    },

    settings_option_show_composer_now_playing() {
      return this.$store.getters.settings_option_show_composer_now_playing
    },

    settings_option_show_composer_for_genre() {
      return this.$store.getters.settings_option_show_composer_for_genre
    },

    composer() {
      if (this.settings_option_show_composer_now_playing) {
        if (
          !this.settings_option_show_composer_for_genre ||
          (this.track.genre &&
            this.settings_option_show_composer_for_genre
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

    settings_option_show_filepath_now_playing() {
      return this.$store.getters.settings_option_show_filepath_now_playing
    },

    filepath() {
      if (this.settings_option_show_filepath_now_playing) {
        return this.track.path
      }
      return null
    }
  },

  watch: {
    player() {
      if (this.interval_id > 0) {
        window.clearTimeout(this.interval_id)
        this.interval_id = 0
      }
      this.item_progress_ms = this.player.item_progress_ms
      if (this.player.state === 'play') {
        this.interval_id = window.setInterval(this.tick, 1000)
      }
    }
  },

  created() {
    this.item_progress_ms = this.player.item_progress_ms
    webapi.player_status().then(({ data }) => {
      this.$store.commit(types.UPDATE_PLAYER_STATUS, data)
      if (this.player.state === 'play') {
        this.interval_id = window.setInterval(this.tick, 1000)
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
    tick() {
      if (!this.is_dragged) {
        if (this.is_live) {
          this.item_progress_ms += 1000
        } else if (this.item_progress_ms + 1000 > this.track.length_ms) {
          this.item_progress_ms = this.track.length_ms
        } else {
          this.item_progress_ms += 1000
        }
      }
    },

    start_dragging() {
      this.is_dragged = true
    },

    end_dragging() {
      this.is_dragged = false
    },

    seek() {
      if (!this.is_live) {
        webapi.player_seek_to_pos(this.item_progress_ms).catch(() => {
          this.item_progress_ms = this.player.item_progress_ms
        })
      }
    },

    open_dialog(item) {
      this.selected_item = item
      this.show_details_modal = true
    }
  }
}
</script>

<style></style>
