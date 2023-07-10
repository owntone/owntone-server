<template>
  <nav
    class="navbar is-block is-white is-fixed-bottom fd-bottom-navbar"
    :style="zindex"
    :class="{
      'is-transparent': is_now_playing_page,
      'is-dark': !is_now_playing_page
    }"
    role="navigation"
    aria-label="player controls"
  >
    <!-- Player menu for desktop -->
    <div
      class="navbar-item has-dropdown has-dropdown-up is-hidden-touch"
      :class="{ 'is-active': show_player_menu }"
    >
      <div class="navbar-dropdown is-right fd-width-auto">
        <div class="navbar-item">
          <!-- Outputs: master volume -->
          <div class="level is-mobile">
            <div class="level-left is-flex-grow-1">
              <div class="level-item is-flex-grow-0">
                <a class="button is-white is-small" @click="toggle_mute_volume">
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
                  <control-slider
                    v-model:value="player.volume"
                    :max="100"
                    @change="change_volume"
                  />
                </div>
              </div>
            </div>
          </div>
        </div>
        <!-- Outputs: master volume -->
        <hr class="my-3" />
        <navbar-item-output
          v-for="output in outputs"
          :key="output.id"
          :output="output"
        />
        <!-- Outputs: stream volume -->
        <hr class="my-3" />
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
                  <control-slider
                    v-model:value="stream_volume"
                    :disabled="!playing"
                    :max="100"
                    :cursor="cursor"
                    @change="change_stream_volume"
                  />
                </div>
              </div>
            </div>
          </div>
        </div>
        <!-- Playback controls -->
        <hr class="my-3" />
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
    <div class="navbar-brand is-flex-grow-1">
      <navbar-item-link :to="{ name: 'queue' }" exact class="mr-auto">
        <mdicon class="icon" name="playlist-play" size="24" />
      </navbar-item-link>
      <navbar-item-link
        v-if="!is_now_playing_page"
        :to="{ name: 'now-playing' }"
        exact
        class="navbar-item fd-is-text-clipped is-expanded is-clipped is-size-7"
      >
        <div class="fd-is-text-clipped">
          <strong v-text="now_playing.title" />
          <br />
          <span v-text="now_playing.artist" />
          <span
            v-if="now_playing.album"
            v-text="$t('navigation.now-playing', { album: now_playing.album })"
          />
        </div>
      </navbar-item-link>
      <player-button-previous
        v-if="is_now_playing_page"
        class="navbar-item px-2"
        :icon_size="24"
      />
      <player-button-seek-back
        v-if="is_now_playing_page"
        :seek_ms="10000"
        class="navbar-item px-2"
        :icon_size="24"
      />
      <player-button-play-pause
        class="navbar-item px-2"
        :icon_size="36"
        show_disabled_message
      />
      <player-button-seek-forward
        v-if="is_now_playing_page"
        :seek_ms="30000"
        class="navbar-item px-2"
        :icon_size="24"
      />
      <player-button-next
        v-if="is_now_playing_page"
        class="navbar-item px-2"
        :icon_size="24"
      />
      <a
        class="navbar-item ml-auto"
        @click="show_player_menu = !show_player_menu"
      >
        <mdicon
          class="icon"
          :name="show_player_menu ? 'chevron-down' : 'chevron-up'"
        />
      </a>
    </div>
    <!-- Player menu for mobile and tablet -->
    <div
      class="navbar-menu is-hidden-desktop"
      :class="{ 'is-active': show_player_menu }"
    >
      <div class="navbar-start" />
      <div class="navbar-end">
        <!-- Repeat/shuffle/consume -->
        <div class="navbar-item">
          <div class="buttons has-addons is-centered">
            <player-button-repeat class="button" />
            <player-button-shuffle class="button" />
            <player-button-consume class="button" />
          </div>
        </div>
        <hr class="my-3" />
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
                  <control-slider
                    v-model:value="player.volume"
                    :max="100"
                    @change="change_volume"
                  />
                </div>
              </div>
            </div>
          </div>
        </div>
        <hr class="my-3" />
        <!-- Outputs: speaker volumes -->
        <navbar-item-output
          v-for="output in outputs"
          :key="output.id"
          :output="output"
        />
        <!-- Outputs: stream volume -->
        <hr class="my-3" />
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
                  <control-slider
                    v-model:value="stream_volume"
                    :disabled="!playing"
                    :max="100"
                    :cursor="cursor"
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
import * as types from '@/store/mutation_types'
import _audio from '@/audio'
import ControlSlider from '@/components/ControlSlider.vue'
import { mdiCancel } from '@mdi/js'
import NavbarItemLink from './NavbarItemLink.vue'
import NavbarItemOutput from './NavbarItemOutput.vue'
import PlayerButtonConsume from '@/components/PlayerButtonConsume.vue'
import PlayerButtonNext from '@/components/PlayerButtonNext.vue'
import PlayerButtonPlayPause from '@/components/PlayerButtonPlayPause.vue'
import PlayerButtonPrevious from '@/components/PlayerButtonPrevious.vue'
import PlayerButtonRepeat from '@/components/PlayerButtonRepeat.vue'
import PlayerButtonSeekBack from '@/components/PlayerButtonSeekBack.vue'
import PlayerButtonSeekForward from '@/components/PlayerButtonSeekForward.vue'
import PlayerButtonShuffle from '@/components/PlayerButtonShuffle.vue'
import webapi from '@/webapi'

export default {
  name: 'NavbarBottom',
  components: {
    ControlSlider,
    NavbarItemLink,
    NavbarItemOutput,
    PlayerButtonConsume,
    PlayerButtonNext,
    PlayerButtonPlayPause,
    PlayerButtonPrevious,
    PlayerButtonRepeat,
    PlayerButtonSeekBack,
    PlayerButtonSeekForward,
    PlayerButtonShuffle
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
      return this.$route.name === 'now-playing'
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
      _audio.setVolume(this.stream_volume / 100)
    }
  }
}
</script>

<style></style>
