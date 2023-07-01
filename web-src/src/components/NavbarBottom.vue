<template>
  <nav
    class="fd-bottom-navbar navbar is-white is-fixed-bottom"
    :style="zindex"
    :class="{
      'is-transparent': is_now_playing_page,
      'is-dark': !is_now_playing_page
    }"
    role="navigation"
    aria-label="player controls"
  >
    <div class="navbar-brand is-flex-grow-1">
      <!-- Link to queue -->
      <navbar-item-link to="/" exact>
        <mdicon class="icon" name="playlist-play" size="24" />
      </navbar-item-link>
      <!-- Now playing artist/title (not visible on "now playing" page) -->
      <router-link
        v-if="!is_now_playing_page"
        to="/now-playing"
        class="navbar-item is-expanded is-clipped"
        active-class="is-active"
        exact
      >
        <div class="is-clipped">
          <p class="is-size-7 fd-is-text-clipped">
            <strong v-text="now_playing.title" />
            <br />
            <span v-text="now_playing.artist" />
            <span
              v-if="now_playing.data_kind === 'url'"
              v-text="
                $t('navigation.now-playing', { album: now_playing.album })
              "
            />
          </p>
        </div>
      </router-link>
      <!-- Skip previous (not visible on "now playing" page) -->
      <player-button-previous
        v-if="is_now_playing_page"
        class="navbar-item ml-auto"
        :icon_size="24"
      />
      <player-button-seek-back
        v-if="is_now_playing_page"
        :seek_ms="10000"
        class="navbar-item"
        :icon_size="24"
      />
      <!-- Play/pause -->
      <player-button-play-pause
        class="navbar-item"
        :icon_size="36"
        show_disabled_message
      />
      <player-button-seek-forward
        v-if="is_now_playing_page"
        :seek_ms="30000"
        class="navbar-item"
        :icon_size="24"
      />
      <!-- Skip next (not visible on "now playing" page) -->
      <player-button-next
        v-if="is_now_playing_page"
        class="navbar-item"
        :icon_size="24"
      />
      <!-- Player menu button (only visible on mobile and tablet) -->
      <a
        class="navbar-item ml-auto is-hidden-desktop"
        @click="show_player_menu = !show_player_menu"
      >
        <mdicon
          class="icon"
          :name="show_player_menu ? 'chevron-down' : 'chevron-up'"
          size="18"
        />
      </a>
      <!-- Player menu dropup menu (only visible on desktop) -->
      <div
        class="navbar-item has-dropdown has-dropdown-up ml-auto is-hidden-touch"
        :class="{ 'is-active': show_player_menu }"
      >
        <a
          class="navbar-link is-arrowless"
          @click="show_player_menu = !show_player_menu"
        >
          <mdicon
            class="icon"
            :name="show_player_menu ? 'chevron-down' : 'chevron-up'"
            size="18"
          />
        </a>
        <div class="navbar-dropdown is-right">
          <div class="navbar-item">
            <!-- Outputs: master volume -->
            <div class="level is-mobile">
              <div class="level-left is-flex-grow-1">
                <div class="level-item is-flex-grow-0">
                  <a
                    class="button is-white is-small"
                    @click="toggle_mute_volume"
                  >
                    <span class="icon"
                      ><mdicon
                        :name="player.volume > 0 ? 'volume-high' : 'volume-off'"
                        size="18"
                    /></span>
                  </a>
                </div>
                <div class="level-item">
                  <div>
                    <p class="heading" v-text="$t('navigation.volume')" />
                    <input
                      v-model="player.volume"
                      class="slider"
                      max="100"
                      type="range"
                      :style="{ '--ratio': player.volume / 100 }"
                      @change="change_volume"
                    />
                  </div>
                </div>
              </div>
            </div>
          </div>
          <!-- Outputs: master volume -->
          <hr class="fd-navbar-divider" />
          <navbar-item-output
            v-for="output in outputs"
            :key="output.id"
            :output="output"
          />
          <!-- Outputs: stream volume -->
          <hr class="fd-navbar-divider" />
          <div class="navbar-item">
            <div class="level is-mobile">
              <div class="level-left is-flex-grow-1">
                <div class="level-item is-flex-grow-0">
                  <a
                    class="button is-clickable is-white is-small"
                    :class="{
                      'has-text-grey-light': !playing && !loading,
                      'is-loading': loading
                    }"
                    @click="togglePlay"
                    ><mdicon class="icon" name="broadcast" size="18" />
                  </a>
                </div>
                <div class="level-item">
                  <div class="is-flex-grow-1">
                    <div
                      class="is-flex is-align-content-center"
                      :class="{ 'has-text-grey-light': !playing }"
                    >
                      <p class="heading" v-text="$t('navigation.stream')" />
                      <a href="stream.mp3" class="heading ml-2" target="_blank"
                        ><mdicon
                          class="icon is-small"
                          name="open-in-new"
                          size="16"
                        />
                      </a>
                    </div>
                    <input
                      v-model="stream_volume"
                      :disabled="!playing"
                      class="slider"
                      :class="{ 'is-inactive': !playing }"
                      max="100"
                      type="range"
                      :style="{
                        '--ratio': stream_volume / 100,
                        '--cursor': $filters.cursor(cursor)
                      }"
                      @change="change_stream_volume"
                    />
                  </div>
                </div>
              </div>
            </div>
          </div>
          <!-- Playback controls -->
          <hr class="fd-navbar-divider" />
          <div class="navbar-item is-justify-content-center">
            <div class="level">
              <div class="level-item">
                <div class="buttons has-addons">
                  <player-button-repeat class="button" />
                  <player-button-shuffle class="button" />
                  <player-button-consume class="button" />
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
    <!-- Player menu (only visible on mobile and tablet) -->
    <div
      class="navbar-menu is-hidden-desktop"
      :class="{ 'is-active': show_player_menu }"
    >
      <div class="navbar-start" />
      <div class="navbar-end">
        <!-- Repeat/shuffle/consume -->
        <div class="navbar-item">
          <div class="buttons is-centered">
            <player-button-repeat class="button" :icon_size="18" />
            <player-button-shuffle class="button" :icon_size="18" />
            <player-button-consume class="button" :icon_size="18" />
          </div>
        </div>
        <hr class="fd-navbar-divider" />
        <!-- Outputs: master volume -->
        <div class="navbar-item">
          <div class="level is-mobile">
            <div class="level-left is-flex-grow-1">
              <div class="level-item is-flex-grow-0">
                <a class="button is-white is-small" @click="toggle_mute_volume">
                  <mdicon
                    class="icon"
                    :name="player.volume > 0 ? 'volume-high' : 'volume-off'"
                    size="18"
                  />
                </a>
              </div>
              <div class="level-item">
                <div class="is-flex-grow-1">
                  <p class="heading" v-text="$t('navigation.volume')" />
                  <input
                    v-model="player.volume"
                    class="slider"
                    max="100"
                    type="range"
                    :style="{ '--ratio': player.volume / 100 }"
                    @change="change_volume"
                  />
                </div>
              </div>
            </div>
          </div>
        </div>
        <!-- Outputs: speaker volumes -->
        <navbar-item-output
          v-for="output in outputs"
          :key="output.id"
          :output="output"
        />
        <!-- Outputs: stream volume -->
        <hr class="fd-navbar-divider" />
        <div class="navbar-item mb-5">
          <div class="level is-mobile">
            <div class="level-left is-flex-grow-1">
              <div class="level-item is-flex-grow-0">
                <a
                  class="button is-clickable is-white is-small"
                  :class="{
                    'has-text-grey-light': !playing && !loading,
                    'is-loading': loading
                  }"
                  @click="togglePlay"
                  ><mdicon class="icon" name="radio-tower" size="16" />
                </a>
              </div>
              <div class="level-item">
                <div class="is-flex-grow-1">
                  <div
                    class="is-flex is-align-content-center"
                    :class="{ 'has-text-grey-light': !playing }"
                  >
                    <p class="heading" v-text="$t('navigation.stream')" />
                    <a href="stream.mp3" class="heading ml-2" target="_blank"
                      ><mdicon
                        class="icon is-small"
                        name="open-in-new"
                        size="16"
                      />
                    </a>
                  </div>
                  <input
                    v-model="stream_volume"
                    :disabled="!playing"
                    class="slider"
                    :class="{ 'is-inactive': !playing }"
                    max="100"
                    type="range"
                    :style="{
                      '--ratio': stream_volume / 100,
                      '--cursor': $filters.cursor(cursor)
                    }"
                    @change="change_stream_volume"
                  />
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </nav>
</template>

<script>
import webapi from '@/webapi'
import _audio from '@/audio'
import { mdiCancel } from '@mdi/js'
import NavbarItemLink from './NavbarItemLink.vue'
import NavbarItemOutput from './NavbarItemOutput.vue'
import PlayerButtonPlayPause from '@/components/PlayerButtonPlayPause.vue'
import PlayerButtonNext from '@/components/PlayerButtonNext.vue'
import PlayerButtonPrevious from '@/components/PlayerButtonPrevious.vue'
import PlayerButtonShuffle from '@/components/PlayerButtonShuffle.vue'
import PlayerButtonConsume from '@/components/PlayerButtonConsume.vue'
import PlayerButtonRepeat from '@/components/PlayerButtonRepeat.vue'
import PlayerButtonSeekBack from '@/components/PlayerButtonSeekBack.vue'
import PlayerButtonSeekForward from '@/components/PlayerButtonSeekForward.vue'
import * as types from '@/store/mutation_types'

export default {
  name: 'NavbarBottom',
  components: {
    NavbarItemLink,
    NavbarItemOutput,
    PlayerButtonPlayPause,
    PlayerButtonNext,
    PlayerButtonPrevious,
    PlayerButtonShuffle,
    PlayerButtonConsume,
    PlayerButtonRepeat,
    PlayerButtonSeekForward,
    PlayerButtonSeekBack
  },

  data() {
    return {
      cursor: mdiCancel,
      old_volume: 0,
      playing: false,
      loading: false,
      stream_volume: 10,
      show_outputs_menu: false,
      show_desktop_outputs_menu: false
    }
  },

  computed: {
    show_player_menu: {
      get() {
        return this.$store.state.show_player_menu
      },
      set(value) {
        this.$store.commit(types.SHOW_PLAYER_MENU, value)
      }
    },

    show_burger_menu() {
      return this.$store.state.show_burger_menu
    },

    zindex() {
      if (this.show_burger_menu) {
        return 'z-index: 20'
      }
      return ''
    },

    now_playing() {
      return this.$store.getters.now_playing
    },
    is_now_playing_page() {
      return this.$route.path === '/now-playing'
    },
    outputs() {
      return this.$store.state.outputs
    },

    player() {
      return this.$store.state.player
    },

    config() {
      return this.$store.state.config
    }
  },

  watch: {
    '$store.state.player.volume'() {
      if (this.player.volume > 0) {
        this.old_volume = this.player.volume
      }
    }
  },

  // On app mounted
  mounted() {
    this.setupAudio()
  },

  // On app destroyed
  unmounted() {
    this.closeAudio()
  },

  methods: {
    on_click_outside_outputs() {
      this.show_outputs_menu = false
    },

    change_volume() {
      webapi.player_volume(this.player.volume)
    },

    toggle_mute_volume() {
      this.player.volume = this.player.volume > 0 ? 0 : this.old_volume
      this.change_volume()
    },

    setupAudio() {
      const a = _audio.setupAudio()

      a.addEventListener('waiting', (e) => {
        this.playing = false
        this.loading = true
      })
      a.addEventListener('playing', (e) => {
        this.playing = true
        this.loading = false
      })
      a.addEventListener('ended', (e) => {
        this.playing = false
        this.loading = false
      })
      a.addEventListener('error', (e) => {
        this.closeAudio()
        this.$store.dispatch('add_notification', {
          text: this.$t('navigation.stream-error'),
          type: 'danger'
        })
        this.playing = false
        this.loading = false
      })
    },

    // Close active audio
    closeAudio() {
      _audio.stopAudio()
      this.playing = false
    },

    playChannel() {
      if (this.playing) {
        return
      }

      const channel = '/stream.mp3'
      this.loading = true
      _audio.playSource(channel)
      _audio.setVolume(this.stream_volume / 100)
    },

    togglePlay() {
      if (this.loading) {
        return
      }
      if (this.playing) {
        return this.closeAudio()
      }
      return this.playChannel()
    },

    change_stream_volume() {
      console.log(this.stream_volume)
      _audio.setVolume(this.stream_volume / 100)
    }
  }
}
</script>

<style></style>
