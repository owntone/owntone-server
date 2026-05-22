<template>
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

<script setup>
import { onBeforeUnmount, onMounted, watch } from 'vue'
import ListNotifications from '@/components/ListNotifications.vue'
import ModalDialogRemotePairing from '@/components/ModalDialogRemotePairing.vue'
import ModalDialogUpdate from '@/components/ModalDialogUpdate.vue'
import NavbarBottom from '@/components/NavbarBottom.vue'
import NavbarTop from '@/components/NavbarTop.vue'
import ReconnectingWebSocket from 'reconnectingwebsocket'
import { useConfigurationStore } from '@/stores/configuration'
import { useI18n } from 'vue-i18n'
import { useLibraryStore } from '@/stores/library'
import { useNotificationsStore } from '@/stores/notifications'
import { useOutputsStore } from './stores/outputs'
import { usePlayerStore } from '@/stores/player'
import { useQueueStore } from '@/stores/queue'
import { useRemotesStore } from './stores/remotes'
import { useServicesStore } from '@/stores/services'
import { useSettingsStore } from '@/stores/settings'
import { useUIStore } from './stores/ui'

const configurationStore = useConfigurationStore()
const libraryStore = useLibraryStore()
const notificationsStore = useNotificationsStore()
const outputsStore = useOutputsStore()
const playerStore = usePlayerStore()
const queueStore = useQueueStore()
const remotesStore = useRemotesStore()
const servicesStore = useServicesStore()
const settingsStore = useSettingsStore()
const uiStore = useUIStore()
const { t } = useI18n()

let socket = null
const handlers = {
  database: [libraryStore.initialise],
  options: [playerStore.initialise],
  outputs: [outputsStore.initialise],
  pairing: [remotesStore.initialise],
  player: [playerStore.initialise],
  queue: [queueStore.initialise],
  services: [servicesStore.initialise],
  settings: [settingsStore.initialise],
  update: [libraryStore.initialise],
  volume: [playerStore.initialise, outputsStore.initialise]
}
const scheduledHandlers = new Map()
const EVENTS = Object.keys(handlers)
const HANDLER_DEBOUNCE_MS = 50

const updateClipping = () => {
  document.documentElement.classList.toggle(
    'is-clipped',
    uiStore.showBurgerMenu || uiStore.showPlayerMenu
  )
}

watch(() => [uiStore.showBurgerMenu, uiStore.showPlayerMenu], updateClipping)

const handleEvents = (events = []) => {
  events.forEach((event) => {
    const list = handlers[event] || []
    list.forEach((handler) => {
      if (!scheduledHandlers.has(handler)) {
        const timeoutId = setTimeout(() => {
          handler()
          scheduledHandlers.delete(handler)
        }, HANDLER_DEBOUNCE_MS)
        scheduledHandlers.set(handler, timeoutId)
      }
    })
  })
}

const createWebsocket = () => {
  const protocol = window.location.protocol.replace('http', 'ws')
  const hostname =
    new URL(import.meta.env.VITE_OWNTONE_URL)?.hostname ||
    window.location.hostname
  const suffix =
    configurationStore.websocket_port || `${window.location.port}/ws`
  const url = `${protocol}//${hostname}:${suffix}`
  return new ReconnectingWebSocket(url, 'notify', {
    maxReconnectInterval: 2000,
    reconnectInterval: 1000
  })
}

const focusHandler = () => handleEvents(EVENTS)
const visibilityHandler = () => {
  if (document.visibilityState === 'visible') {
    handleEvents(EVENTS)
  }
}

const openWebsocket = () => {
  socket = createWebsocket()
  socket.onopen = () => {
    socket.send(JSON.stringify({ notify: EVENTS }))
    handleEvents(EVENTS)
  }
  socket.onmessage = ({ data }) => handleEvents(JSON.parse(data).notify)
}

const connect = async () => {
  try {
    await configurationStore.initialise()
    uiStore.hideSingles = configurationStore.hide_singles
    document.title = configurationStore.library_name
    openWebsocket()
  } catch {
    notificationsStore.add({
      text: t('server.connection-failed') ?? 'Connection failed',
      topic: 'connection',
      type: 'danger'
    })
  }
}

onMounted(() => {
  connect()
  window.addEventListener('focus', focusHandler)
  document.addEventListener('visibilitychange', visibilityHandler)
})

onBeforeUnmount(() => {
  window.removeEventListener('focus', focusHandler)
  document.removeEventListener('visibilitychange', visibilityHandler)
  scheduledHandlers.forEach((timeoutId) => clearTimeout(timeoutId))
  scheduledHandlers.clear()
  socket?.close()
})
</script>
