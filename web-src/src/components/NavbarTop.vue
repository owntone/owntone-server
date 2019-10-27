<template>
  <nav class="navbar is-light is-fixed-top" role="navigation" aria-label="main navigation">
    <div class="navbar-brand">
      <navbar-item-link to="/playlists">
        <span class="icon"><i class="mdi mdi-library-music"></i></span>
      </navbar-item-link>
      <navbar-item-link to="/music">
        <span class="icon"><i class="mdi mdi-music"></i></span>
      </navbar-item-link>
      <navbar-item-link to="/podcasts" v-if="podcasts.tracks > 0">
        <span class="icon"><i class="mdi mdi-microphone"></i></span>
      </navbar-item-link>
      <navbar-item-link to="/audiobooks" v-if="audiobooks.tracks > 0">
        <span class="icon"><i class="mdi mdi-book-open-variant"></i></span>
      </navbar-item-link>
      <navbar-item-link to="/files">
        <span class="icon"><i class="mdi mdi-folder-open"></i></span>
      </navbar-item-link>
      <navbar-item-link to="/search">
        <span class="icon"><i class="mdi mdi-magnify"></i></span>
      </navbar-item-link>

      <div class="navbar-burger" @click="update_show_burger_menu" :class="{ 'is-active': show_burger_menu }">
        <span></span>
        <span></span>
        <span></span>
      </div>
    </div>

    <div class="navbar-menu" :class="{ 'is-active': show_burger_menu }">
      <div class="navbar-start">
      </div>

      <div class="navbar-end">

        <!-- Outputs dropdown -->
        <div class="navbar-item has-dropdown"
            :class="{ 'is-active': show_outputs_menu, 'is-hoverable': !show_outputs_menu && !show_settings_menu }"
            @click="show_outputs_menu = !show_outputs_menu"
            v-click-outside="on_click_outside_outputs">
          <a class="navbar-link is-arrowless"><span class="icon is-hidden-mobile is-hidden-tablet-only"><i class="mdi mdi-volume-high"></i></span> <span class="is-hidden-desktop has-text-weight-bold">Volume</span></a>

          <div class="navbar-dropdown is-right">
            <div class="navbar-item">
              <!-- Outputs: master volume -->
              <div class="level is-mobile">
                <div class="level-left fd-expanded">
                  <div class="level-item" style="flex-grow: 0;">
                    <a class="button is-white is-small" @click="toggle_mute_volume">
                      <span class="icon"><i class="mdi mdi-18px" :class="{ 'mdi-volume-off': player.volume <= 0, 'mdi-volume-high': player.volume > 0 }"></i></span>
                    </a>
                  </div>
                  <div class="level-item fd-expanded">
                    <div class="fd-expanded">
                      <p class="heading">Volume</p>
                      <range-slider
                        class="slider fd-has-action"
                        min="0"
                        max="100"
                        step="1"
                        :value="player.volume"
                        @change="set_volume">
                      </range-slider>
                    </div>
                  </div>
                </div>
              </div>
            </div>

            <!-- Outputs: master volume -->
            <hr class="navbar-divider">
            <navbar-item-output v-for="output in outputs" :key="output.id" :output="output"></navbar-item-output>

            <!-- Outputs: stream volume -->
            <hr class="navbar-divider">
            <div class="navbar-item">
              <div class="level is-mobile">
                <div class="level-left fd-expanded">
                  <div class="level-item" style="flex-grow: 0;">
                    <a class="button is-white is-small" :class="{ 'is-loading': loading }"><span class="icon fd-has-action" :class="{ 'has-text-grey-light': !playing && !loading, 'is-loading': loading }" @click="togglePlay"><i class="mdi mdi-18px mdi-radio-tower"></i></span></a>
                  </div>
                  <div class="level-item fd-expanded">
                    <div class="fd-expanded">
                      <p class="heading" :class="{ 'has-text-grey-light': !playing }">HTTP stream <a href="/stream.mp3"><span class="is-lowercase">(stream.mp3)</span></a></p>
                      <range-slider
                        class="slider fd-has-action"
                        min="0"
                        max="100"
                        step="1"
                        :disabled="!playing"
                        :value="stream_volume"
                        @change="set_stream_volume">
                      </range-slider>
                    </div>
                  </div>
                </div>
              </div>
            </div>

            <!-- Playback controls -->
            <hr class="navbar-divider">
            <div class="navbar-item">
              <div class="level is-mobile">
                <div class="level-left">
                  <div class="level-item">
                    <div class="buttons has-addons">
                      <player-button-previous class="button"></player-button-previous>
                      <player-button-play-pause class="button"></player-button-play-pause>
                      <player-button-next class="button"></player-button-next>
                    </div>
                  </div>
                  <div class="level-item">
                    <div class="buttons has-addons">
                      <player-button-repeat class="button is-light"></player-button-repeat>
                      <player-button-shuffle class="button is-light"></player-button-shuffle>
                      <player-button-consume class="button is-light"></player-button-consume>
                    </div>
                  </div>
                </div>
              </div>
            </div>
          </div>
        </div>

        <!-- Settings drop down -->
        <div class="navbar-item has-dropdown"
            :class="{ 'is-active': show_settings_menu, 'is-hoverable': !show_outputs_menu && !show_settings_menu }"
            @click="show_settings_menu = !show_settings_menu"
            v-click-outside="on_click_outside_settings">
          <a class="navbar-link is-arrowless"><span class="icon is-hidden-mobile is-hidden-tablet-only"><i class="mdi mdi-settings"></i></span> <span class="is-hidden-desktop has-text-weight-bold">forked-daapd</span></a>

          <div class="navbar-dropdown is-right">
            <a class="navbar-item" href="/admin.html">Admin</a>
            <hr class="navbar-divider">
            <navbar-item-link to="/settings/webinterface">Settings</navbar-item-link>
            <navbar-item-link to="/about">About</navbar-item-link>
          </div>
        </div>
      </div>
    </div>
  </nav>
</template>

<script>
import webapi from '@/webapi'
import _audio from '@/audio'
import NavbarItemLink from './NavbarItemLink'
import NavbarItemOutput from './NavbarItemOutput'
import PlayerButtonPlayPause from './PlayerButtonPlayPause'
import PlayerButtonNext from './PlayerButtonNext'
import PlayerButtonPrevious from './PlayerButtonPrevious'
import PlayerButtonShuffle from './PlayerButtonShuffle'
import PlayerButtonConsume from './PlayerButtonConsume'
import PlayerButtonRepeat from './PlayerButtonRepeat'
import RangeSlider from 'vue-range-slider'
import * as types from '@/store/mutation_types'

export default {
  name: 'NavbarTop',
  components: { NavbarItemLink, NavbarItemOutput, PlayerButtonPlayPause, PlayerButtonNext, PlayerButtonPrevious, PlayerButtonShuffle, PlayerButtonConsume, PlayerButtonRepeat, RangeSlider },

  data () {
    return {
      old_volume: 0,

      playing: false,
      loading: false,
      stream_volume: 10,

      show_outputs_menu: false,
      show_settings_menu: false
    }
  },

  computed: {
    outputs () {
      return this.$store.state.outputs
    },

    player () {
      return this.$store.state.player
    },

    config () {
      return this.$store.state.config
    },

    library () {
      return this.$store.state.library
    },

    audiobooks () {
      return this.$store.state.audiobooks_count
    },

    podcasts () {
      return this.$store.state.podcasts_count
    },

    show_burger_menu () {
      return this.$store.state.show_burger_menu
    }
  },

  methods: {
    update_show_burger_menu: function () {
      this.$store.commit(types.SHOW_BURGER_MENU, !this.show_burger_menu)
    },

    on_click_outside_outputs () {
      this.show_outputs_menu = false
    },

    on_click_outside_settings () {
      this.show_settings_menu = false
    },

    set_volume: function (newVolume) {
      webapi.player_volume(newVolume)
    },

    toggle_mute_volume: function () {
      if (this.player.volume > 0) {
        this.set_volume(0)
      } else {
        this.set_volume(this.old_volume)
      }
    },

    setupAudio: function () {
      const a = _audio.setupAudio()

      a.addEventListener('waiting', e => {
        this.playing = false
        this.loading = true
      })
      a.addEventListener('playing', e => {
        this.playing = true
        this.loading = false
      })
      a.addEventListener('ended', e => {
        this.playing = false
        this.loading = false
      })
      a.addEventListener('error', e => {
        this.closeAudio()
        this.$store.dispatch('add_notification', { text: 'HTTP stream error: failed to load stream or stopped loading due to network problem', type: 'danger' })
        this.playing = false
        this.loading = false
      })
    },

    // close active audio
    closeAudio: function () {
      _audio.stopAudio()
      this.playing = false
    },

    playChannel: function () {
      if (this.playing) {
        return
      }

      const channel = '/stream.mp3'
      this.loading = true
      _audio.playSource(channel)
      _audio.setVolume(this.stream_volume / 100)
    },

    togglePlay: function () {
      if (this.loading) {
        return
      }
      if (this.playing) {
        return this.closeAudio()
      }
      return this.playChannel()
    },

    set_stream_volume: function (newVolume) {
      this.stream_volume = newVolume
      _audio.setVolume(this.stream_volume / 100)
    }
  },

  watch: {
    '$store.state.player.volume' () {
      if (this.player.volume > 0) {
        this.old_volume = this.player.volume
      }
    }
  },

  // on app mounted
  mounted () {
    this.setupAudio()
  },

  // on app destroyed
  destroyed () {
    this.closeAudio()
  }
}
</script>

<style>
</style>
