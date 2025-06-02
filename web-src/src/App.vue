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
      handlers: {},
      scheduledHandlers: new Map()
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
    this.handlers = {
      database: [this.libraryStore.initialise],
      lastfm: [this.servicesStore.initialiseLastfm],
      options: [this.playerStore.initialise],
      outputs: [this.outputsStore.initialise],
      pairing: [this.remotesStore.initialise],
      player: [this.playerStore.initialise],
      queue: [this.queueStore.initialise],
      settings: [this.settingsStore.initialise],
      spotify: [this.servicesStore.initialiseSpotify],
      update: [this.libraryStore.initialise],
      volume: [this.playerStore.initialise, this.outputsStore.initialise]
    }
    this.$router.beforeEach(async (to, from, next) => {
      await this.configurationStore.initialise()
      this.updateClipping()
      if (!(to.path === from.path && to.hash)) {
        this.$Progress.start()
      }
      next()
    })
    this.$router.afterEach(() => {
      this.$Progress.finish()
    })
    this.connect()
  },
  beforeUnmount() {
    this.scheduledHandlers.forEach((timeoutId) => clearTimeout(timeoutId))
    this.scheduledHandlers.clear()
  },
  methods: {
    async connect() {
      try {
        await this.configurationStore.initialise()
        this.uiStore.hideSingles = this.configurationStore.hide_singles
        document.title = this.configurationStore.library_name
        this.openWebsocket()
      } catch {
        this.notificationsStore.add({
          text: this.$t('server.connection-failed'),
          topic: 'connection',
          type: 'danger'
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
    handleEvents(events = []) {
      events.forEach((event) => {
        const handlers = this.handlers[event] || []
        handlers.forEach((handler) => {
          if (!this.scheduledHandlers.has(handler)) {
            const timeoutId = setTimeout(() => {
              handler.call(this)
              this.scheduledHandlers.delete(handler)
            }, 50)
            this.scheduledHandlers.set(handler, timeoutId)
          }
        })
      })
    },
    openWebsocket() {
      const socket = this.createWebsocket()
      const events = [
        'database',
        'lastfm',
        'options',
        'outputs',
        'pairing',
        'player',
        'queue',
        'settings',
        'spotify',
        'update',
        'volume'
      ]
      socket.onopen = () => {
        socket.send(JSON.stringify({ notify: events }))
        this.handleEvents(events)
      }
      window.addEventListener('focus', () => {
        this.handleEvents(events)
      })
      document.addEventListener('visibilitychange', () => {
        if (document.visibilityState === 'visible') {
          this.handleEvents(events)
        }
      })
      socket.onmessage = (response) => {
        const notifiedEvents = JSON.parse(response.data).notify
        this.handleEvents(notifiedEvents)
      }
    },
    updateClipping() {
      if (this.uiStore.showBurgerMenu || this.uiStore.showPlayerMenu) {
        document.querySelector('html').classList.add('is-clipped')
      } else {
        document.querySelector('html').classList.remove('is-clipped')
      }
    }
  }
}
</script>
