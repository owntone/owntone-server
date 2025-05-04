<template>
  <vue-progress-bar class="has-background-primary" />
  <navbar-top />
  <router-view v-slot="{ Component }">
    <component :is="Component" />
  </router-view>
  <modal-dialog-remote-pairing
    :show="remotesStore.active"
    @close="remotesStore.active = false"
  />
  <modal-dialog-update
    :show="uiStore.showUpdateDialog"
    @close="uiStore.showUpdateDialog = false"
  />
  <list-notifications v-show="!uiStore.showBurgerMenu" />
  <navbar-bottom />
  <div
    v-show="uiStore.showBurgerMenu || uiStore.showPlayerMenu"
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
import configuration from '@/api/configuration'
import services from '@/api/services'
import { useConfigurationStore } from '@/stores/configuration'
import { useLibraryStore } from '@/stores/library'
import { useNotificationsStore } from '@/stores/notifications'
import { useOutputsStore } from './stores/outputs'
import { usePlayerStore } from '@/stores/player'
import { useQueueStore } from '@/stores/queue'
import { useRemotesStore } from './stores/remotes'
import { useServicesStore } from '@/stores/services'
import { useSettingsStore } from '@/stores/settings'
import { useUIStore } from './stores/ui'

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
      timerId: 0
    }
  },
  watch: {
    'uiStore.showBurgerMenu'() {
      this.updateClipping()
    },
    'uiStore.showPlayerMenu'() {
      this.updateClipping()
    }
  },
  created() {
    this.connect()
    this.$router.beforeEach((to, from, next) => {
      this.updateClipping()
      if (!(to.path === from.path && to.hash)) {
        if (to.meta.progress) {
          this.$Progress.parseMeta(to.meta.progress)
        }
        this.$Progress.start()
      }
      next()
    })
    this.$router.afterEach(() => {
      this.$Progress.finish()
    })
  },
  methods: {
    connect() {
      configuration
        .list()
        .then((data) => {
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
        this.updateOutputs()
        this.updatePlayer()
        this.updateLibrary()
        this.updateSettings()
        this.updateQueue()
        this.updateSpotify()
        this.updateLastfm()
        this.updateRemotes()
      }

      let updateThrottled = false
      const updateInfo = () => {
        if (updateThrottled) {
          return
        }
        this.updateOutputs()
        this.updatePlayer()
        this.updateLibrary()
        this.updateSettings()
        this.updateQueue()
        this.updateSpotify()
        this.updateLastfm()
        this.updateRemotes()
        updateThrottled = true
        setTimeout(() => {
          updateThrottled = false
        }, 500)
      }

      window.addEventListener('focus', updateInfo)
      document.addEventListener('visibilitychange', () => {
        if (document.visibilityState === 'visible') {
          updateInfo()
        }
      })

      socket.onmessage = (response) => {
        const data = JSON.parse(response.data)
        const notify = new Set(data.notify || [])
        const handlers = [
          { events: ['update', 'database'], handler: this.updateLibrary },
          {
            events: ['player', 'options', 'volume'],
            handler: this.updatePlayer
          },
          { events: ['outputs', 'volume'], handler: this.updateOutputs },
          { events: ['queue'], handler: this.updateQueue },
          { events: ['spotify'], handler: this.updateSpotify },
          { events: ['lastfm'], handler: this.updateLastfm },
          { events: ['pairing'], handler: this.updateRemotes }
        ]
        handlers.forEach(({ handler, events }) => {
          if (events.some((key) => notify.has(key))) {
            handler.call(this)
          }
        })
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
      if (this.uiStore.showBurgerMenu || this.uiStore.showPlayerMenu) {
        document.querySelector('html').classList.add('is-clipped')
      } else {
        document.querySelector('html').classList.remove('is-clipped')
      }
    },
    updateLastfm() {
      services.lastfm().then((data) => {
        this.servicesStore.lastfm = data
      })
    },
    updateLibrary() {
      this.libraryStore.initialise()
    },
    updateOutputs() {
      this.outputsStore.initialise()
    },
    updateRemotes() {
      this.remotesStore.initialise()
    },
    updatePlayer() {
      this.playerStore.initialise()
    },
    updateQueue() {
      this.queueStore.initialise()
    },
    updateSettings() {
      this.settingsStore.initialise()
    },
    updateSpotify() {
      services.spotify().then((data) => {
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
  }
}
</script>
