import Vue from 'vue'
import Vuex from 'vuex'
import * as types from './mutation_types'

Vue.use(Vuex)

export default new Vuex.Store({
  state: {
    config: {
      'websocket_port': 0,
      'version': '',
      'buildoptions': [ ]
    },
    library: {
      'artists': 0,
      'albums': 0,
      'songs': 0,
      'db_playtime': 0,
      'updating': false
    },
    audiobooks_count: { },
    podcasts_count: { },
    outputs: [ ],
    player: {
      'state': 'stop',
      'repeat': 'off',
      'consume': false,
      'shuffle': false,
      'volume': 0,
      'item_id': 0,
      'item_length_ms': 0,
      'item_progress_ms': 0
    },
    queue: {
      'version': 0,
      'count': 0,
      'items': [ ]
    },
    spotify: {},

    spotify_new_releases: [],
    spotify_featured_playlists: [],

    notifications: {
      'next_id': 1,
      'list': []
    },
    recent_searches: [],

    hide_singles: false,
    show_only_next_items: false,
    show_burger_menu: false
  },

  getters: {
    now_playing: state => {
      var item = state.queue.items.find(function (item) {
        return item.id === state.player.item_id
      })
      return (item === undefined) ? {} : item
    }
  },

  mutations: {
    [types.UPDATE_CONFIG] (state, config) {
      state.config = config
    },
    [types.UPDATE_LIBRARY_STATS] (state, libraryStats) {
      state.library = libraryStats
    },
    [types.UPDATE_LIBRARY_AUDIOBOOKS_COUNT] (state, count) {
      state.audiobooks_count = count
    },
    [types.UPDATE_LIBRARY_PODCASTS_COUNT] (state, count) {
      state.podcasts_count = count
    },
    [types.UPDATE_OUTPUTS] (state, outputs) {
      state.outputs = outputs
    },
    [types.UPDATE_PLAYER_STATUS] (state, playerStatus) {
      state.player = playerStatus
    },
    [types.UPDATE_QUEUE] (state, queue) {
      state.queue = queue
    },
    [types.UPDATE_SPOTIFY] (state, spotify) {
      state.spotify = spotify
    },
    [types.SPOTIFY_NEW_RELEASES] (state, newReleases) {
      state.spotify_new_releases = newReleases
    },
    [types.SPOTIFY_FEATURED_PLAYLISTS] (state, featuredPlaylists) {
      state.spotify_featured_playlists = featuredPlaylists
    },
    [types.ADD_NOTIFICATION] (state, notification) {
      if (notification.topic) {
        var index = state.notifications.list.findIndex(elem => elem.topic === notification.topic)
        if (index >= 0) {
          state.notifications.list.splice(index, 1, notification)
          return
        }
      }
      state.notifications.list.push(notification)
    },
    [types.DELETE_NOTIFICATION] (state, notification) {
      const index = state.notifications.list.indexOf(notification)

      if (index !== -1) {
        state.notifications.list.splice(index, 1)
      }
    },
    [types.ADD_RECENT_SEARCH] (state, query) {
      var index = state.recent_searches.findIndex(elem => elem === query)
      if (index >= 0) {
        state.recent_searches.splice(index, 1)
      }

      state.recent_searches.splice(0, 0, query)

      if (state.recent_searches.length > 5) {
        state.recent_searches.pop()
      }
    },
    [types.HIDE_SINGLES] (state, hideSingles) {
      state.hide_singles = hideSingles
    },
    [types.SHOW_ONLY_NEXT_ITEMS] (state, showOnlyNextItems) {
      state.show_only_next_items = showOnlyNextItems
    },
    [types.SHOW_BURGER_MENU] (state, showBurgerMenu) {
      state.show_burger_menu = showBurgerMenu
    }
  },

  actions: {
    add_notification ({ commit, state }, notification) {
      const newNotification = {
        'id': state.notifications.next_id++,
        'type': notification.type,
        'text': notification.text,
        'topic': notification.topic,
        'timeout': notification.timeout
      }

      commit(types.ADD_NOTIFICATION, newNotification)

      if (notification.timeout > 0) {
        setTimeout(() => {
          commit(types.DELETE_NOTIFICATION, newNotification)
        }, notification.timeout)
      }
    }
  }
})
