<template>
  <section class="hero fd-is-fullheight">
    <div class="hero-head fd-has-padding-left-right">
      <div class="container has-text-centered fd-has-margin-top">
        <h1 class="title is-4">
          {{ now_playing.title }}
        </h1>
        <h2 class="title is-6">
          {{ now_playing.artist }}
        </h2>
        <h2 class="subtitle is-6 has-text-grey has-text-weight-bold" v-if="composer">
            {{ composer }}
        </h2>
        <h3 class="subtitle is-6">
          {{ now_playing.album }}
        </h3>
      </div>
    </div>
    <div class="hero-body fd-is-fullheight-body has-text-centered" v-show="artwork_visible">
      <img :src="artwork_url" class="fd-has-shadow fd-image-fullheight fd-has-action"
        @load="artwork_loaded"
        @error="artwork_error"
        @click="open_dialog(now_playing)">
    </div>
    <div class="hero-body fd-is-fullheight-body has-text-centered" v-show="!artwork_visible">
      <a @click="open_dialog(now_playing)" class="button is-white is-medium">
        <span class="icon has-text-grey-light"><i class="mdi mdi-information-outline"></i></span>
      </a>
    </div>
    <div class="hero-foot fd-has-padding-left-right">
      <div class="container has-text-centered fd-has-margin-bottom">
        <p class="control has-text-centered fd-progress-now-playing">
          <range-slider
            class="seek-slider fd-has-action"
            min="0"
            :max="state.item_length_ms"
            :value="item_progress_ms"
            :disabled="state.state === 'stop'"
            step="1000"
            @change="seek" >
          </range-slider>
        </p>
        <p class="content">
          <span>{{ item_progress_ms | duration }} / {{ now_playing.length_ms | duration }}</span>
        </p>
        <div class="buttons has-addons is-centered">
          <player-button-previous class="button is-medium"></player-button-previous>
          <player-button-play-pause class="button is-medium" icon_style="mdi-36px"></player-button-play-pause>
          <player-button-next class="button is-medium"></player-button-next>
          <player-button-repeat class="button is-medium is-light"></player-button-repeat>
          <player-button-shuffle class="button is-medium is-light"></player-button-shuffle>
          <player-button-consume class="button is-medium is-light"></player-button-consume>
        </div>
      </div>
      <modal-dialog-queue-item :show="show_details_modal" :item="selected_item" @close="show_details_modal = false" />
    </div>
  </section>
</template>

<script>
import ModalDialogQueueItem from '@/components/ModalDialogQueueItem'
import PlayerButtonPlayPause from '@/components/PlayerButtonPlayPause'
import PlayerButtonNext from '@/components/PlayerButtonNext'
import PlayerButtonPrevious from '@/components/PlayerButtonPrevious'
import PlayerButtonShuffle from '@/components/PlayerButtonShuffle'
import PlayerButtonConsume from '@/components/PlayerButtonConsume'
import PlayerButtonRepeat from '@/components/PlayerButtonRepeat'
import RangeSlider from 'vue-range-slider'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'

export default {
  name: 'PageNowPlaying',
  components: { ModalDialogQueueItem, PlayerButtonPlayPause, PlayerButtonNext, PlayerButtonPrevious, PlayerButtonShuffle, PlayerButtonConsume, PlayerButtonRepeat, RangeSlider },

  data () {
    return {
      item_progress_ms: 0,
      interval_id: 0,
      artwork_visible: false,

      show_details_modal: false,
      selected_item: {}
    }
  },

  created () {
    this.item_progress_ms = this.state.item_progress_ms
    webapi.player_status().then(({ data }) => {
      this.$store.commit(types.UPDATE_PLAYER_STATUS, data)
      if (this.state.state === 'play') {
        this.interval_id = window.setInterval(this.tick, 1000)
      }
    })
  },

  destroyed () {
    if (this.interval_id > 0) {
      window.clearTimeout(this.interval_id)
      this.interval_id = 0
    }
  },

  computed: {
    state () {
      return this.$store.state.player
    },

    now_playing () {
      return this.$store.getters.now_playing
    },

    artwork_url: function () {
      return webapi.artwork_url_append_size_params(this.now_playing.artwork_url)
    },

    settings_option_show_composer_now_playing () {
      return this.$store.getters.settings_option_show_composer_now_playing
    },

    settings_option_show_composer_for_genre () {
      return this.$store.getters.settings_option_show_composer_for_genre
    },

    composer () {
      if (this.settings_option_show_composer_now_playing) {
        if (!this.settings_option_show_composer_for_genre ||
            (this.now_playing.genre &&
            this.settings_option_show_composer_for_genre.toLowerCase()
              .split(',')
              .findIndex(elem => this.now_playing.genre.toLowerCase().indexOf(elem.trim()) >= 0) >= 0)) {
          return this.now_playing.composer
        }
      }
      return null
    }
  },

  methods: {
    tick: function () {
      this.item_progress_ms += 1000
    },

    seek: function (newPosition) {
      webapi.player_seek(newPosition).catch(() => {
        this.item_progress_ms = this.state.item_progress_ms
      })
    },

    artwork_loaded: function () {
      this.artwork_visible = true
    },

    artwork_error: function () {
      this.artwork_visible = false
    },

    open_dialog: function (item) {
      this.selected_item = item
      this.show_details_modal = true
    }
  },

  watch: {
    'state' () {
      if (this.interval_id > 0) {
        window.clearTimeout(this.interval_id)
        this.interval_id = 0
      }
      this.item_progress_ms = this.state.item_progress_ms
      if (this.state.state === 'play') {
        this.interval_id = window.setInterval(this.tick, 1000)
      }
    }
  }
}
</script>

<style>
</style>
