<template>
  <section class="hero">
    <div class="hero-body">
      <div class="container has-text-centered">
        <p class="heading">NOW PLAYING</p>
        <h1 class="title is-3">
          {{ now_playing.title }}
        </h1>
        <h2 class="title is-5">
          {{ now_playing.artist }}
        </h2>
        <h3 class="subtitle is-5">
          {{ now_playing.album }}
        </h3>
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
        <p class="control has-text-centered">
          <player-button-previous class="button is-medium"></player-button-previous>
          <player-button-play-pause class="button is-medium" icon_style="mdi-36px"></player-button-play-pause>
          <player-button-next class="button is-medium"></player-button-next>
          <player-button-repeat class="button is-medium is-light"></player-button-repeat>
          <player-button-shuffle class="button is-medium is-light"></player-button-shuffle>
          <player-button-consume class="button is-medium is-light"></player-button-consume>
        </p>
      </div>
    </div>
  </section>
</template>

<script>
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
  components: { PlayerButtonPlayPause, PlayerButtonNext, PlayerButtonPrevious, PlayerButtonShuffle, PlayerButtonConsume, PlayerButtonRepeat, RangeSlider },

  data () {
    return {
      item_progress_ms: 0,
      interval_id: 0
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
