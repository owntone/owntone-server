<template>
  <nav
    class="navbar is-block is-white is-fixed-bottom fd-bottom-navbar"
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
                  <mdicon
                    class="icon"
                    :name="player.volume > 0 ? 'volume-high' : 'volume-off'"
                    size="18"
                  />
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
                  class="button is-white is-small"
                  :class="{
                    'has-text-grey-light': !playing && !loading,
                    'is-loading': loading
                  }"
                  @click="togglePlay"
                >
                  <mdicon class="icon" name="broadcast" size="18" />
                </a>
              </div>
              <div class="level-item">
                <div class="is-flex-grow-1">
                  <div
                    class="is-flex is-align-content-center"
                    :class="{ 'has-text-grey-light': !playing }"
                  >
                    <p class="heading" v-text="$t('navigation.stream')" />
                    <a href="stream.mp3" class="heading ml-2" target="_blank">
                      <mdicon
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
                    @change="changeStreamVolume"
                  />
                </div>
              </div>
            </div>
          </div>
        </div>
        <hr class="my-3" />
        <div class="navbar-item is-justify-content-center">
          <div class="buttons has-addons">
            <player-button-repeat class="button" />
            <player-button-shuffle class="button" />
            <player-button-consume class="button" />
            <player-button-lyrics class="button" />
          </div>
        </div>
      </div>
    </div>
    <div class="navbar-brand is-flex-grow-1">
      <navbar-item-link :to="{ name: 'queue' }" class="mr-auto">
        <mdicon class="icon" name="playlist-play" size="24" />
      </navbar-item-link>
      <navbar-item-link
        v-if="!is_now_playing_page"
        :to="{ name: 'now-playing' }"
        exact
        class="is-expanded is-clipped is-size-7"
      >
        <div class="fd-is-text-clipped">
          <strong v-text="current.title" />
          <br />
          <span v-text="current.artist" />
          <span
            v-if="current.album"
            v-text="$t('navigation.now-playing', { album: current.album })"
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
      <div class="navbar-item">
        <div class="buttons has-addons is-centered">
          <player-button-repeat class="button" />
          <player-button-shuffle class="button" />
          <player-button-consume class="button" />
          <player-button-lyrics class="button" />
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
                class="button is-white is-small"
                :class="{
                  'has-text-grey-light': !playing && !loading,
                  'is-loading': loading
                }"
                @click="togglePlay"
              >
                <mdicon class="icon" name="radio-tower" size="16" />
              </a>
            </div>
            <div class="level-item">
              <div class="is-flex-grow-1">
                <div
                  class="is-flex is-align-content-center"
                  :class="{ 'has-text-grey-light': !playing }"
                >
                  <p class="heading" v-text="$t('navigation.stream')" />
                  <a href="stream.mp3" class="heading ml-2" target="_blank">
                    <mdicon
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
                  @change="changeStreamVolume"
                />
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  </nav>
</template>

<script>
import ControlSlider from '@/components/ControlSlider.vue'
import NavbarItemLink from '@/components/NavbarItemLink.vue'
import NavbarItemOutput from '@/components/NavbarItemOutput.vue'
import PlayerButtonConsume from '@/components/PlayerButtonConsume.vue'
import PlayerButtonLyrics from '@/components/PlayerButtonLyrics.vue'
import PlayerButtonNext from '@/components/PlayerButtonNext.vue'
import PlayerButtonPlayPause from '@/components/PlayerButtonPlayPause.vue'
import PlayerButtonPrevious from '@/components/PlayerButtonPrevious.vue'
import PlayerButtonRepeat from '@/components/PlayerButtonRepeat.vue'
import PlayerButtonSeekBack from '@/components/PlayerButtonSeekBack.vue'
import PlayerButtonSeekForward from '@/components/PlayerButtonSeekForward.vue'
import PlayerButtonShuffle from '@/components/PlayerButtonShuffle.vue'
import audio from '@/lib/Audio'
import { mdiCancel } from '@mdi/js'
import { useNotificationsStore } from '@/stores/notifications'
import { useOutputsStore } from '@/stores/outputs'
import { usePlayerStore } from '@/stores/player'
import { useQueueStore } from '@/stores/queue'
import { useUIStore } from '@/stores/ui'
import webapi from '@/webapi'

export default {
  name: 'NavbarBottom',
  components: {
    ControlSlider,
    NavbarItemLink,
    NavbarItemOutput,
    PlayerButtonConsume,
    PlayerButtonLyrics,
    PlayerButtonNext,
    PlayerButtonPlayPause,
    PlayerButtonPrevious,
    PlayerButtonRepeat,
    PlayerButtonSeekBack,
    PlayerButtonSeekForward,
    PlayerButtonShuffle
  },

  setup() {
    return {
      notificationsStore: useNotificationsStore(),
      outputsStore: useOutputsStore(),
      playerStore: usePlayerStore(),
      queueStore: useQueueStore(),
      uiStore: useUIStore()
    }
  },

  data() {
    return {
      cursor: mdiCancel,
      loading: false,
      old_volume: 0,
      playing: false,
      show_desktop_outputs_menu: false,
      show_outputs_menu: false,
      stream_volume: 10
    }
  },

  computed: {
    is_now_playing_page() {
      return this.$route.name === 'now-playing'
    },
    current() {
      return this.queueStore.current
    },
    outputs() {
      return this.outputsStore.outputs
    },
    player() {
      return this.playerStore
    },
    show_player_menu: {
      get() {
        return this.uiStore.show_player_menu
      },
      set(value) {
        this.uiStore.show_player_menu = value
      }
    }
  },

  watch: {
    'playerStore.volume'() {
      if (this.player.volume > 0) {
        this.old_volume = this.player.volume
      }
    }
  },

  unmounted() {
    this.closeAudio()
  },

  methods: {
    changeStreamVolume() {
      audio.setVolume(this.stream_volume / 100)
    },
    change_volume() {
      webapi.player_volume(this.player.volume)
    },
    closeAudio() {
      audio.stop()
      this.playing = false
      this.loading = false
    },
    on_click_outside_outputs() {
      this.show_outputs_menu = false
    },
    playChannel() {
      this.loading = true
      audio.play('/stream.mp3')
      this.changeStreamVolume()
      const a = audio.audio
      if (a) {
        a.addEventListener('waiting', () => {
          this.playing = false
          this.loading = true
        })
        a.addEventListener('playing', () => {
          this.playing = true
          this.loading = false
        })
        a.addEventListener('ended', () => {
          this.playing = false
          this.loading = false
        })
        a.addEventListener('error', () => {
          this.closeAudio()
        })
      }
    },
    togglePlay() {
      if (this.loading) {
        return
      }
      if (this.playing) {
        this.closeAudio()
      } else {
        this.playChannel()
      }
    },
    toggle_mute_volume() {
      this.player.volume = this.player.volume > 0 ? 0 : this.old_volume
      this.change_volume()
    }
  }
}
</script>

<style></style>
