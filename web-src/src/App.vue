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
import { onBeforeUnmount, onMounted, ref, watch } from 'vue'
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

const handlers = ref({})
const scheduledHandlers = ref(new Map())

const updateClipping = () => {
  const html = document.querySelector('html')
  if (uiStore.showBurgerMenu || uiStore.showPlayerMenu) {
    html.classList.add('is-clipped')
  } else {
    html.classList.remove('is-clipped')
  }
}

watch(() => uiStore.showBurgerMenu, updateClipping)
watch(() => uiStore.showPlayerMenu, updateClipping)

const handleEvents = (events = []) => {
  events.forEach((event) => {
    const list = handlers.value[event] || []
    list.forEach((handler) => {
      if (!scheduledHandlers.value.has(handler)) {
        const timeoutId = setTimeout(() => {
          handler.call()
          scheduledHandlers.value.delete(handler)
        }, 50)
        scheduledHandlers.value.set(handler, timeoutId)
      }
    })
  })
}

const createWebsocket = () => {
  const protocol = window.location.protocol.replace('http', 'ws')
  const hostname =
    (import.meta.env.DEV &&
      URL.parse(import.meta.env.VITE_OWNTONE_URL)?.hostname) ||
    window.location.hostname
  const suffix =
    configurationStore.websocket_port || `${window.location.port}/ws`
  const url = `${protocol}${hostname}:${suffix}`
  return new ReconnectingWebSocket(url, 'notify', {
    maxReconnectInterval: 2000,
    reconnectInterval: 1000
  })
}

const openWebsocket = () => {
  const socket = createWebsocket()
  const events = [
    'database',
    'options',
    'outputs',
    'pairing',
    'player',
    'queue',
    'settings',
    'services',
    'update',
    'volume'
  ]
  socket.onopen = () => {
    socket.send(JSON.stringify({ notify: events }))
    handleEvents(events)
  }
  window.addEventListener('focus', () => {
    handleEvents(events)
  })
  document.addEventListener('visibilitychange', () => {
    if (document.visibilityState === 'visible') {
      handleEvents(events)
    }
  })
  socket.onmessage = (response) => {
    const notifiedEvents = JSON.parse(response.data).notify
    handleEvents(notifiedEvents)
  }
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
  handlers.value = {
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
  connect()
})

onBeforeUnmount(() => {
  scheduledHandlers.value.forEach((timeoutId) => clearTimeout(timeoutId))
  scheduledHandlers.value.clear()
})
</script>
