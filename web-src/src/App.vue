<template>
  <vue-progress-bar class="has-background-primary" />
  <navbar-top />
  <router-view v-slot="{ Component }">
    <component :is="Component" />
  </router-view>
  <modal-dialog-remote-pairing
    :show="pairingActive"
    @close="pairingActive = false"
  />
  <modal-dialog-update
    :show="showUpdateDialog"
    @close="showUpdateDialog = false"
  />
  <list-notifications v-show="!showBurgerMenu" />
  <navbar-bottom />
  <div
    v-show="showBurgerMenu || showPlayerMenu"
    class="overlay-fullscreen"
    @click="uiStore.hideMenus"
  />
</template>

<script>
import ListNotifications from '@/components/ListNotifications.vue'
import ModalDialogRemotePairing from '@/components/ModalDialogRemotePairing.vue'
import ModalDialogUpdate from '@/components/ModalDialogUpdate.vue'
import NavbarBottom from '@/components/NavbarBottom.vue'
import NavbarTop from '@/components/NavbarTop.vue'
import ReconnectingWebSocket from 'reconnectingwebsocket'
import { useConfigurationStore } from '@/stores/configuration'
import { useLibraryStore } from '@/stores/library'
import { useLyricsStore } from '@/stores/lyrics'
import { useNotificationsStore } from '@/stores/notifications'
import { useOutputsStore } from './stores/outputs'
import { usePlayerStore } from '@/stores/player'
import { useQueueStore } from '@/stores/queue'
import { useRemotesStore } from './stores/remotes'
import { useServicesStore } from '@/stores/services'
import { useSettingsStore } from '@/stores/settings'
import { useUIStore } from './stores/ui'
import webapi from '@/webapi'

export default {
  name: 'App',
  components: {
    ListNotifications,
    ModalDialogRemotePairing,
    ModalDialogUpdate,
    NavbarBottom,
    NavbarTop
  },
  setup() {
    return {
      configurationStore: useConfigurationStore(),
      libraryStore: useLibraryStore(),
      lyricsStore: useLyricsStore(),
      notificationsStore: useNotificationsStore(),
      outputsStore: useOutputsStore(),
      playerStore: usePlayerStore(),
      queueStore: useQueueStore(),
      remotesStore: useRemotesStore(),
      servicesStore: useServicesStore(),
      settingsStore: useSettingsStore(),
      uiStore: useUIStore()
    }
  },
  data() {
    return {
      pairingActive: false,
      timerId: 0
    }
  },
  computed: {
    showBurgerMenu: {
      get() {
        return this.uiStore.showBurgerMenu
      },
      set(value) {
        this.uiStore.showBurgerMenu = value
      }
    },
    showPlayerMenu: {
      get() {
        return this.uiStore.showPlayerMenu
      },
      set(value) {
        this.uiStore.showPlayerMenu = value
      }
    },
    showUpdateDialog: {
      get() {
        return this.uiStore.showUpdateDialog
      },
      set(value) {
        this.uiStore.showUpdateDialog = value
      }
    }
  },
  watch: {
    showBurgerMenu() {
      this.updateClipping()
    },
    showPlayerMenu() {
      this.updateClipping()
    }
  },
  created() {
    this.connect()
    // Hook the progress bar to start before we move router-view
    this.$router.beforeEach((to, from, next) => {
      if (!(to.path === from.path && to.hash)) {
        if (to.meta.progress) {
          this.$Progress.parseMeta(to.meta.progress)
        }
        this.$Progress.start()
      }
      next()
    })
    // Hook the progress bar to finish after we've finished moving router-view
    this.$router.afterEach((to, from) => {
      this.$Progress.finish()
    })
  },
  methods: {
    connect() {
      webapi
        .config()
        .then(({ data }) => {
          this.configurationStore.$state = data
          this.uiStore.hideSingles = data.hide_singles
          document.title = data.library_name
          this.openWebsocket()
          this.$Progress.finish()
        })
        .catch(() => {
          this.notificationsStore.add({
            text: this.$t('server.connection-failed'),
            topic: 'connection',
            type: 'danger'
          })
        })
    },
    openWebsocket() {
      const socket = this.createWebsocket()
      const vm = this
      socket.onopen = () => {
        socket.send(
          JSON.stringify({
            notify: [
              'update',
              'database',
              'player',
              'options',
              'outputs',
              'volume',
              'queue',
              'spotify',
              'lastfm',
              'pairing'
            ]
          })
        )
        vm.updateOutputs()
        vm.updatePlayerStatus()
        vm.updateLibraryStats()
        vm.updateSettings()
        vm.updateQueue()
        vm.updateSpotify()
        vm.updateLastfm()
        vm.updatePairing()
      }

      /*
       * When the app becomes active, force an update of all information,
       * because we may have missed notifications while the app was inactive.
       * There are two relevant events - focus and visibilitychange, so we
       * throttle the updates to avoid multiple redundant updates.
       */
      let updateThrottled = false

      const updateInfo = () => {
        if (updateThrottled) {
          return
        }
        vm.updateOutputs()
        vm.updatePlayerStatus()
        vm.updateLibraryStats()
        vm.updateSettings()
        vm.updateQueue()
        vm.updateSpotify()
        vm.updateLastfm()
        vm.updatePairing()
        updateThrottled = true
        setTimeout(() => {
          updateThrottled = false
        }, 500)
      }

      /*
       * These events are fired when the window becomes active in different
       * ways. When this happens, we should update 'now playing' info, etc.
       */
      window.addEventListener('focus', updateInfo)
      document.addEventListener('visibilitychange', () => {
        if (document.visibilityState === 'visible') {
          updateInfo()
        }
      })

      socket.onmessage = (response) => {
        const data = JSON.parse(response.data)
        if (
          data.notify.includes('update') ||
          data.notify.includes('database')
        ) {
          vm.updateLibraryStats()
        }
        if (
          data.notify.includes('player') ||
          data.notify.includes('options') ||
          data.notify.includes('volume')
        ) {
          vm.updatePlayerStatus()
        }
        if (data.notify.includes('outputs') || data.notify.includes('volume')) {
          vm.updateOutputs()
        }
        if (data.notify.includes('queue')) {
          vm.updateQueue()
        }
        if (data.notify.includes('spotify')) {
          vm.updateSpotify()
        }
        if (data.notify.includes('lastfm')) {
          vm.updateLastfm()
        }
        if (data.notify.includes('pairing')) {
          vm.updatePairing()
        }
      }
    },
    createWebsocket() {
      const protocol = window.location.protocol.replace('http', 'ws')
      const hostname =
        (import.meta.env.DEV &&
          URL.parse(import.meta.env.VITE_OWNTONE_URL)?.hostname) ||
        window.location.hostname
      const suffix =
        this.configurationStore.websocket_port || `${window.location.port}/ws`
      const url = `${protocol}${hostname}:${suffix}`
      return new ReconnectingWebSocket(url, 'notify', {
        maxReconnectInterval: 2000,
        reconnectInterval: 1000
      })
    },
    updateClipping() {
      if (this.showBurgerMenu || this.showPlayerMenu) {
        document.querySelector('html').classList.add('is-clipped')
      } else {
        document.querySelector('html').classList.remove('is-clipped')
      }
    },
    updateLastfm() {
      webapi.lastfm().then(({ data }) => {
        this.servicesStore.lastfm = data
      })
    },
    updateLibraryStats() {
      webapi.library_stats().then(({ data }) => {
        this.libraryStore.$state = data
      })
      webapi.library_count('scan_kind is rss').then(({ data }) => {
        this.libraryStore.rss = data
      })
    },
    updateLyrics() {
      const track = this.queueStore.current
      if (track?.track_id) {
        webapi.library_track(track.track_id).then(({ data }) => {
          this.lyricsStore.lyrics = data.lyrics
        })
      } else {
        this.lyricsStore.$reset()
      }
    },
    updateOutputs() {
      webapi.outputs().then(({ data }) => {
        this.outputsStore.outputs = data.outputs
      })
    },
    updatePairing() {
      webapi.pairing().then(({ data }) => {
        this.remotesStore.$state = data
        this.pairingActive = data.active
      })
    },
    updatePlayerStatus() {
      webapi.player_status().then(({ data }) => {
        this.playerStore.$state = data
        this.updateLyrics()
      })
    },
    updateQueue() {
      webapi.queue().then(({ data }) => {
        this.queueStore.$state = data
        this.updateLyrics()
      })
    },
    updateSettings() {
      webapi.settings().then(({ data }) => {
        this.settingsStore.$state = data
      })
    },
    updateSpotify() {
      webapi.spotify().then(({ data }) => {
        this.servicesStore.spotify = data
        if (this.timerId > 0) {
          window.clearTimeout(this.timerId)
          this.timerId = 0
        }
        if (data.webapi_token_expires_in > 0 && data.webapi_token) {
          this.timerId = window.setTimeout(
            this.updateSpotify,
            1000 * data.webapi_token_expires_in
          )
        }
      })
    }
  },
  template: '<App/>'
}
</script>
