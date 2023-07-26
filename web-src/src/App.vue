<template>
  <div id="app">
    <navbar-top />
    <vue-progress-bar class="fd-progress-bar has-background-info" />
    <router-view v-slot="{ Component }">
      <component :is="Component" />
    </router-view>

    <modal-dialog-remote-pairing
      :show="pairing_active"
      @close="pairing_active = false"
    />
    <modal-dialog-update
      :show="show_update_dialog"
      @close="show_update_dialog = false"
    />
    <notification-list v-show="!show_burger_menu" />
    <navbar-bottom />
    <div
      v-show="show_burger_menu || show_player_menu"
      class="fd-overlay-fullscreen"
      @click="show_burger_menu = show_player_menu = false"
    />
  </div>
</template>

<script>
import NavbarTop from '@/components/NavbarTop.vue'
import NavbarBottom from '@/components/NavbarBottom.vue'
import NotificationList from '@/components/NotificationList.vue'
import ModalDialogRemotePairing from '@/components/ModalDialogRemotePairing.vue'
import ModalDialogUpdate from '@/components/ModalDialogUpdate.vue'
import webapi from '@/webapi'
import * as types from '@/store/mutation_types'
import ReconnectingWebSocket from 'reconnectingwebsocket'

export default {
  name: 'App',
  components: {
    NavbarTop,
    NavbarBottom,
    NotificationList,
    ModalDialogRemotePairing,
    ModalDialogUpdate
  },

  data() {
    return {
      token_timer_id: 0,
      reconnect_attempts: 0,
      pairing_active: false
    }
  },

  computed: {
    show_burger_menu: {
      get() {
        return this.$store.state.show_burger_menu
      },
      set(value) {
        this.$store.commit(types.SHOW_BURGER_MENU, value)
      }
    },
    show_player_menu: {
      get() {
        return this.$store.state.show_player_menu
      },
      set(value) {
        this.$store.commit(types.SHOW_PLAYER_MENU, value)
      }
    },
    show_update_dialog: {
      get() {
        return this.$store.state.show_update_dialog
      },
      set(value) {
        this.$store.commit(types.SHOW_UPDATE_DIALOG, value)
      }
    }
  },

  watch: {
    show_burger_menu() {
      this.update_is_clipped()
    },
    show_player_menu() {
      this.update_is_clipped()
    }
  },

  created() {
    this.connect()

    //  Start the progress bar on app start
    this.$Progress.start()

    //  Hook the progress bar to start before we move router-view
    this.$router.beforeEach((to, from, next) => {
      if (to.meta.show_progress && !(to.path === from.path && to.hash)) {
        if (to.meta.progress !== undefined) {
          const meta = to.meta.progress
          this.$Progress.parseMeta(meta)
        }
        this.$Progress.start()
      }
      next()
    })

    //  Hook the progress bar to finish after we've finished moving router-view
    this.$router.afterEach((to, from) => {
      if (to.meta.show_progress) {
        this.$Progress.finish()
      }
    })
  },

  methods: {
    connect() {
      webapi
        .config()
        .then(({ data }) => {
          this.$store.commit(types.UPDATE_CONFIG, data)
          this.$store.commit(types.HIDE_SINGLES, data.hide_singles)
          document.title = data.library_name

          this.open_ws()
          this.$Progress.finish()
        })
        .catch(() => {
          this.$store.dispatch('add_notification', {
            text: this.$t('server.connection-failed'),
            type: 'danger',
            topic: 'connection'
          })
        })
    },

    open_ws() {
      if (this.$store.state.config.websocket_port <= 0) {
        this.$store.dispatch('add_notification', {
          text: this.$t('server.missing-port'),
          type: 'danger'
        })
        return
      }

      const vm = this

      let protocol = 'ws://'
      if (window.location.protocol === 'https:') {
        protocol = 'wss://'
      }

      let wsUrl =
        protocol +
        window.location.hostname +
        ':' +
        vm.$store.state.config.websocket_port

      if (import.meta.env.DEV && import.meta.env.VITE_OWNTONE_URL) {
        // If we are running in development mode, construct the websocket url
        // from the host of the environment variable VITE_OWNTONE_URL
        const owntoneUrl = new URL(import.meta.env.VITE_OWNTONE_URL)
        wsUrl =
          protocol +
          owntoneUrl.hostname +
          ':' +
          vm.$store.state.config.websocket_port
      }

      const socket = new ReconnectingWebSocket(wsUrl, 'notify', {
        reconnectInterval: 1000,
        maxReconnectInterval: 2000
      })

      socket.onopen = function () {
        vm.reconnect_attempts = 0
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

      // When the app becomes active, force an update of all information, because we
      // may have missed notifications while the app was inactive.
      // There are two relevant events (focus and visibilitychange), so we throttle
      // the updates to avoid multiple redundant updates
      var update_throttled = false

      function update_info() {
        if (update_throttled) {
          return
        }

        vm.update_outputs()
        vm.update_player_status()
        vm.update_library_stats()
        vm.update_settings()
        vm.update_queue()
        vm.update_spotify()
        vm.update_lastfm()
        vm.update_pairing()

        update_throttled = true
        setTimeout(() => {
          update_throttled = false
        }, 500)
      }

      // These events are fired when the window becomes active in different ways
      // When this happens, we should update 'now playing' info etc
      window.addEventListener('focus', update_info)
      document.addEventListener('visibilitychange', () => {
        if (document.visibilityState === 'visible') {
          update_info()
        }
      })

      socket.onmessage = function (response) {
        const data = JSON.parse(response.data)
        if (
          data.notify.includes('update') ||
          data.notify.includes('database')
        ) {
          vm.update_library_stats()
        }
        if (
          data.notify.includes('player') ||
          data.notify.includes('options') ||
          data.notify.includes('volume')
        ) {
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

    update_library_stats() {
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

    update_outputs() {
      webapi.outputs().then(({ data }) => {
        this.$store.commit(types.UPDATE_OUTPUTS, data.outputs)
      })
    },

    update_player_status() {
      webapi.player_status().then(({ data }) => {
        this.$store.commit(types.UPDATE_PLAYER_STATUS, data)
      })
    },

    update_queue() {
      webapi.queue().then(({ data }) => {
        this.$store.commit(types.UPDATE_QUEUE, data)
      })
    },

    update_settings() {
      webapi.settings().then(({ data }) => {
        this.$store.commit(types.UPDATE_SETTINGS, data)
      })
    },

    update_lastfm() {
      webapi.lastfm().then(({ data }) => {
        this.$store.commit(types.UPDATE_LASTFM, data)
      })
    },

    update_spotify() {
      webapi.spotify().then(({ data }) => {
        this.$store.commit(types.UPDATE_SPOTIFY, data)

        if (this.token_timer_id > 0) {
          window.clearTimeout(this.token_timer_id)
          this.token_timer_id = 0
        }
        if (data.webapi_token_expires_in > 0 && data.webapi_token) {
          this.token_timer_id = window.setTimeout(
            this.update_spotify,
            1000 * data.webapi_token_expires_in
          )
        }
      })
    },

    update_pairing() {
      webapi.pairing().then(({ data }) => {
        this.$store.commit(types.UPDATE_PAIRING, data)
        this.pairing_active = data.active
      })
    },

    update_is_clipped() {
      if (this.show_burger_menu || this.show_player_menu) {
        document.querySelector('html').classList.add('is-clipped')
      } else {
        document.querySelector('html').classList.remove('is-clipped')
      }
    }
  },
  template: '<App/>'
}
</script>

<style></style>
