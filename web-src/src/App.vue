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
      updateThrottled: false
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
        this.handleEvents(JSON.parse(response.data).notify)
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
    handleEvents(notifications = []) {
      if (this.updateThrottled) {
        return
      }
      const handlers = [
        {
          events: ['update', 'database'],
          handler: this.libraryStore.initialise
        },
        {
          events: ['player', 'options', 'volume'],
          handler: this.playerStore.initialise
        },
        {
          events: ['outputs', 'volume'],
          handler: this.outputsStore.initialise
        },
        { events: ['queue'], handler: this.queueStore.initialise },
        { events: ['settings'], handler: this.settingsStore.initialise },
        { events: ['spotify'], handler: this.servicesStore.initialiseSpotify },
        { events: ['lastfm'], handler: this.servicesStore.initialiseLastfm },
        { events: ['pairing'], handler: this.remotesStore.initialise }
      ]
      const notificationSet = new Set(notifications)
      handlers.forEach(({ handler, events }) => {
        if (events.some((key) => notificationSet.has(key))) {
          handler.call(this)
        }
      })
      this.updateThrottled = true
      setTimeout(() => {
        this.updateThrottled = false
      }, 500)
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
