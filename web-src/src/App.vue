<template>
  <div id="app">
    <navbar-top />
    <vue-progress-bar class="fd-progress-bar" />
    <router-view v-slot="{ Component }">
      <component :is="Component" class="fd-page" />
    </router-view>

    <modal-dialog-remote-pairing :show="pairing_active" @close="pairing_active = false" />
    <modal-dialog-update
        :show="show_update_dialog"
        @close="show_update_dialog = false" />
    <notifications v-show="!show_burger_menu" />
    <navbar-bottom />
    <div class="fd-overlay-fullscreen" v-show="show_burger_menu || show_player_menu"
        @click="show_burger_menu = show_player_menu = false"></div>
  </div>
</template>

<script>
import NavbarTop from '@/components/NavbarTop.vue'
import NavbarBottom from '@/components/NavbarBottom.vue'
import Notifications from '@/components/Notifications.vue'
import ModalDialogRemotePairing from '@/components/ModalDialogRemotePairing.vue'
import ModalDialogUpdate from '@/components/ModalDialogUpdate.vue'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'
import ReconnectingWebSocket from 'reconnectingwebsocket'
import moment from 'moment'

export default {
  name: 'App',
  components: { NavbarTop, NavbarBottom, Notifications, ModalDialogRemotePairing, ModalDialogUpdate },
  template: '<App/>',

  data () {
    return {
      token_timer_id: 0,
      reconnect_attempts: 0,
      pairing_active: false
    }
  },

  computed: {
    show_burger_menu: {
      get () {
        return this.$store.state.show_burger_menu
      },
      set (value) {
        this.$store.commit(types.SHOW_BURGER_MENU, value)
      }
    },
    show_player_menu: {
      get () {
        return this.$store.state.show_player_menu
      },
      set (value) {
        this.$store.commit(types.SHOW_PLAYER_MENU, value)
      }
    },
    show_update_dialog: {
      get () {
        return this.$store.state.show_update_dialog
      },
      set (value) {
        this.$store.commit(types.SHOW_UPDATE_DIALOG, value)
      }
    }
  },

  created: function () {
    moment.locale(navigator.language)
    this.connect()

    //  Start the progress bar on app start
    this.$Progress.start()

    //  Hook the progress bar to start before we move router-view
    this.$router.beforeEach((to, from, next) => {
      if (to.meta.show_progress) {
        if (to.meta.progress !== undefined) {
          const meta = to.meta.progress
          this.$Progress.parseMeta(meta)
        }
        this.$Progress.start()
      }
      next()
    })

    //  hook the progress bar to finish after we've finished moving router-view
    this.$router.afterEach((to, from) => {
      if (to.meta.show_progress) {
        this.$Progress.finish()
      }
    })
  },

  methods: {
    connect: function () {
      this.$store.dispatch('add_notification', { text: 'Connecting to OwnTone server', type: 'info', topic: 'connection', timeout: 2000 })

      webapi.config().then(({ data }) => {
        this.$store.commit(types.UPDATE_CONFIG, data)
        this.$store.commit(types.HIDE_SINGLES, data.hide_singles)
        document.title = data.library_name

        this.open_ws()
        this.$Progress.finish()
      }).catch(() => {
        this.$store.dispatch('add_notification', { text: 'Failed to connect to OwnTone server', type: 'danger', topic: 'connection' })
      })
    },

    open_ws: function () {
      if (this.$store.state.config.websocket_port <= 0) {
        this.$store.dispatch('add_notification', { text: 'Missing websocket port', type: 'danger' })
        return
      }

      const vm = this

      let protocol = 'ws://'
      if (window.location.protocol === 'https:') {
        protocol = 'wss://'
      }

      let wsUrl = protocol + window.location.hostname + ':' + vm.$store.state.config.websocket_port
      if (import.meta.env.NODE_ENV === 'development' && import.meta.env.VUE_APP_WEBSOCKET_SERVER) {
        // If we are running in the development server, use the websocket url configured in .env.development
        wsUrl = import.meta.env.VUE_APP_WEBSOCKET_SERVER
      }

      const socket = new ReconnectingWebSocket(
        wsUrl,
        'notify',
        { reconnectInterval: 3000 }
      )

      socket.onopen = function () {
        vm.$store.dispatch('add_notification', { text: 'Connection to server established', type: 'primary', topic: 'connection', timeout: 2000 })
        vm.reconnect_attempts = 0
        socket.send(JSON.stringify({ notify: ['update', 'database', 'player', 'options', 'outputs', 'volume', 'queue', 'spotify', 'lastfm', 'pairing'] }))

        vm.update_outputs()
        vm.update_player_status()
        vm.update_library_stats()
        vm.update_settings()
        vm.update_queue()
        vm.update_spotify()
        vm.update_lastfm()
        vm.update_pairing()
      }
      socket.onclose = function () {
        // vm.$store.dispatch('add_notification', { text: 'Connection closed', type: 'danger', timeout: 2000 })
      }
      socket.onerror = function () {
        vm.reconnect_attempts++
        vm.$store.dispatch('add_notification', { text: 'Connection lost. Reconnecting ... (' + vm.reconnect_attempts + ')', type: 'danger', topic: 'connection' })
      }
      socket.onmessage = function (response) {
        const data = JSON.parse(response.data)
        if (data.notify.includes('update') || data.notify.includes('database')) {
          vm.update_library_stats()
        }
        if (data.notify.includes('player') || data.notify.includes('options') || data.notify.includes('volume')) {
          vm.update_player_status()
        }
        if (data.notify.includes('outputs') || data.notify.includes('volume')) {
          vm.update_outputs()
        }
        if (data.notify.includes('queue')) {
          vm.update_queue()
        }
        if (data.notify.includes('spotify')) {
          vm.update_spotify()
        }
        if (data.notify.includes('lastfm')) {
          vm.update_lastfm()
        }
        if (data.notify.includes('pairing')) {
          vm.update_pairing()
        }
      }
    },

    update_library_stats: function () {
      webapi.library_stats().then(({ data }) => {
        this.$store.commit(types.UPDATE_LIBRARY_STATS, data)
      })
      webapi.library_count('media_kind is audiobook').then(({ data }) => {
        this.$store.commit(types.UPDATE_LIBRARY_AUDIOBOOKS_COUNT, data)
      })
      webapi.library_count('media_kind is podcast').then(({ data }) => {
        this.$store.commit(types.UPDATE_LIBRARY_PODCASTS_COUNT, data)
      })
      webapi.library_count('scan_kind is rss').then(({ data }) => {
        this.$store.commit(types.UPDATE_LIBRARY_RSS_COUNT, data)
      })
    },

    update_outputs: function () {
      webapi.outputs().then(({ data }) => {
        this.$store.commit(types.UPDATE_OUTPUTS, data.outputs)
      })
    },

    update_player_status: function () {
      webapi.player_status().then(({ data }) => {
        this.$store.commit(types.UPDATE_PLAYER_STATUS, data)
      })
    },

    update_queue: function () {
      webapi.queue().then(({ data }) => {
        this.$store.commit(types.UPDATE_QUEUE, data)
      })
    },

    update_settings: function () {
      webapi.settings().then(({ data }) => {
        this.$store.commit(types.UPDATE_SETTINGS, data)
      })
    },

    update_lastfm: function () {
      webapi.lastfm().then(({ data }) => {
        this.$store.commit(types.UPDATE_LASTFM, data)
      })
    },

    update_spotify: function () {
      webapi.spotify().then(({ data }) => {
        this.$store.commit(types.UPDATE_SPOTIFY, data)

        if (this.token_timer_id > 0) {
          window.clearTimeout(this.token_timer_id)
          this.token_timer_id = 0
        }
        if (data.webapi_token_expires_in > 0 && data.webapi_token) {
          this.token_timer_id = window.setTimeout(this.update_spotify, 1000 * data.webapi_token_expires_in)
        }
      })
    },

    update_pairing: function () {
      webapi.pairing().then(({ data }) => {
        this.$store.commit(types.UPDATE_PAIRING, data)
        this.pairing_active = data.active
      })
    },

    update_is_clipped: function () {
      if (this.show_burger_menu || this.show_player_menu) {
        document.querySelector('html').classList.add('is-clipped')
      } else {
        document.querySelector('html').classList.remove('is-clipped')
      }
    }
  },

  watch: {
    'show_burger_menu' () {
      this.update_is_clipped()
    },
    'show_player_menu' () {
      this.update_is_clipped()
    }
  }
}
</script>

<style>
</style>
