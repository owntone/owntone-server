<template>
  <nav class="navbar is-light is-fixed-top" role="navigation" aria-label="main navigation">
    <div class="navbar-brand">
      <router-link to="/playlists" class="navbar-item" active-class="is-active">
        <span class="icon"><i class="mdi mdi-library-music"></i></span>
      </router-link>
      <router-link to="/music" class="navbar-item" active-class="is-active">
        <span class="icon"><i class="mdi mdi-music"></i></span>
      </router-link>
      <router-link to="/podcasts" class="navbar-item" active-class="is-active" v-if="podcasts.tracks > 0">
        <span class="icon"><i class="mdi mdi-microphone"></i></span>
      </router-link>
      <router-link to="/audiobooks" class="navbar-item" active-class="is-active" v-if="audiobooks.tracks > 0">
        <span class="icon"><i class="mdi mdi-book-open-variant"></i></span>
      </router-link>
      <router-link to="/search" class="navbar-item" active-class="is-active">
        <span class="icon"><i class="mdi mdi-magnify"></i></span>
      </router-link>

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
        <div class="navbar-item has-dropdown is-hoverable">
          <a class="navbar-link"><span class="icon is-hidden-mobile is-hidden-tablet-only"><i class="mdi mdi-volume-high"></i></span> <span class="is-hidden-desktop">Volume</span></a>

          <div class="navbar-dropdown is-right">
            <div class="navbar-item">
              <div class="level is-mobile">
                <div class="level-left fd-expanded">
                  <div class="level-item" style="flex-grow: 0;">
                    <span class="icon"><i class="mdi mdi-18px mdi-volume-high"></i></span>
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
            <hr class="navbar-divider">
            <nav-bar-item-output v-for="output in outputs" :key="output.id" :output="output"></nav-bar-item-output>

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
        <div class="navbar-item has-dropdown is-hoverable">
          <a class="navbar-link"><span class="icon is-hidden-mobile is-hidden-tablet-only"><i class="mdi mdi-settings"></i></span> <span class="is-hidden-desktop">Settings</span></a>

          <div class="navbar-dropdown is-right">
            <a class="navbar-item" href="/admin.html">Admin</a>
            <hr class="navbar-divider">
            <a class="navbar-item" v-on:click="open_about">
              <div>
                <p class="title is-7">forked-daapd</p>
                <p class="subtitle is-7">{{ config.version }}</p>
              </div>
            </a>
          </div>
        </div>
      </div>
    </div>
  </nav>
</template>

<script>
import webapi from '@/webapi'
import NavBarItemOutput from './NavBarItemOutput'
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
  components: { NavBarItemOutput, PlayerButtonPlayPause, PlayerButtonNext, PlayerButtonPrevious, PlayerButtonShuffle, PlayerButtonConsume, PlayerButtonRepeat, RangeSlider },

  data () {
    return {
      search_query: ''
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

    set_volume: function (newVolume) {
      webapi.player_volume(newVolume)
    },

    open_about: function () {
      this.$store.commit(types.SHOW_BURGER_MENU, false)
      this.$router.push({ path: '/about' })
    }
  }
}
</script>

<style>
</style>
