import { createStore } from 'vuex'
import * as types from './mutation_types'

export default createStore({
  state() {
    return {
      config: {
        websocket_port: 0,
        version: '',
        buildoptions: []
      },
      settings: {
        categories: []
      },
      library: {
        artists: 0,
        albums: 0,
        songs: 0,
        db_playtime: 0,
        updating: false
      },
      audiobooks_count: {},
      podcasts_count: {},
      rss_count: {},
      outputs: [],
      player: {
        state: 'stop',
        repeat: 'off',
        consume: false,
        shuffle: false,
        volume: 0,
        item_id: 0,
        item_length_ms: 0,
        item_progress_ms: 0
      },
      queue: {
        version: 0,
        count: 0,
        items: []
      },
      lastfm: {},
      spotify: {},
      pairing: {},

      spotify_new_releases: [],
      spotify_featured_playlists: [],

      notifications: {
        next_id: 1,
        list: []
      },

      search_path: '/search/library',
      recent_searches: [],

      hide_singles: false,
      hide_spotify: false,
      artists_sort: 'Name',
      artist_albums_sort: 'Name',
      albums_sort: 'Name',
      show_only_next_items: false,
      show_burger_menu: false,
      show_player_menu: false,
      show_update_dialog: false,
      update_dialog_scan_kind: ''
    }
  },

  getters: {
    now_playing: (state) => {
      const item = state.queue.items.find(function (item) {
        return item.id === state.player.item_id
      })
      return item === undefined ? {} : item
    },

    settings_webinterface: (state) => {
      if (state.settings) {
        return state.settings.categories.find(
          (elem) => elem.name === 'webinterface'
        )
      }
      return null
    },

    settings_option_recently_added_limit: (state, getters) => {
      if (getters.settings_webinterface) {
        const option = getters.settings_webinterface.options.find(
          (elem) => elem.name === 'recently_added_limit'
        )
        if (option) {
          return option.value
        }
      }
      return 100
    },

    settings_option_show_composer_now_playing: (state, getters) => {
      if (getters.settings_webinterface) {
        const option = getters.settings_webinterface.options.find(
          (elem) => elem.name === 'show_composer_now_playing'
        )
        if (option) {
          return option.value
        }
      }
      return false
    },

    settings_option_show_composer_for_genre: (state, getters) => {
      if (getters.settings_webinterface) {
        const option = getters.settings_webinterface.options.find(
          (elem) => elem.name === 'show_composer_for_genre'
        )
        if (option) {
          return option.value
        }
      }
      return null
    },

    settings_category: (state) => (categoryName) => {
      return state.settings.categories.find(
        (elem) => elem.name === categoryName
      )
    },

    settings_option: (state) => (categoryName, optionName) => {
      const category = state.settings.categories.find(
        (elem) => elem.name === categoryName
      )
      if (!category) {
        return {}
      }
      return category.options.find((elem) => elem.name === optionName)
    }
  },

  mutations: {
    [types.UPDATE_CONFIG](state, config) {
      state.config = config
    },
    [types.UPDATE_SETTINGS](state, settings) {
      state.settings = settings
    },
    [types.UPDATE_SETTINGS_OPTION](state, option) {
      const settingCategory = state.settings.categories.find(
        (elem) => elem.name === option.category
      )
      const settingOption = settingCategory.options.find(
        (elem) => elem.name === option.name
      )
      settingOption.value = option.value
    },
    [types.UPDATE_LIBRARY_STATS](state, libraryStats) {
      state.library = libraryStats
    },
    [types.UPDATE_LIBRARY_AUDIOBOOKS_COUNT](state, count) {
      state.audiobooks_count = count
    },
    [types.UPDATE_LIBRARY_PODCASTS_COUNT](state, count) {
      state.podcasts_count = count
    },
    [types.UPDATE_LIBRARY_RSS_COUNT](state, count) {
      state.rss_count = count
    },
    [types.UPDATE_OUTPUTS](state, outputs) {
      state.outputs = outputs
    },
    [types.UPDATE_PLAYER_STATUS](state, playerStatus) {
      state.player = playerStatus
    },
    [types.UPDATE_QUEUE](state, queue) {
      state.queue = queue
    },
    [types.UPDATE_LASTFM](state, lastfm) {
      state.lastfm = lastfm
    },
    [types.UPDATE_SPOTIFY](state, spotify) {
      state.spotify = spotify
    },
    [types.UPDATE_PAIRING](state, pairing) {
      state.pairing = pairing
    },
    [types.SPOTIFY_NEW_RELEASES](state, newReleases) {
      state.spotify_new_releases = newReleases
    },
    [types.SPOTIFY_FEATURED_PLAYLISTS](state, featuredPlaylists) {
      state.spotify_featured_playlists = featuredPlaylists
    },
    [types.ADD_NOTIFICATION](state, notification) {
      if (notification.topic) {
        const index = state.notifications.list.findIndex(
          (elem) => elem.topic === notification.topic
        )
        if (index >= 0) {
          state.notifications.list.splice(index, 1, notification)
          return
        }
      }
      state.notifications.list.push(notification)
    },
    [types.DELETE_NOTIFICATION](state, notification) {
      const index = state.notifications.list.indexOf(notification)

      if (index !== -1) {
        state.notifications.list.splice(index, 1)
      }
    },
    [types.SEARCH_PATH](state, searchPath) {
      state.search_path = searchPath
    },
    [types.ADD_RECENT_SEARCH](state, query) {
      const index = state.recent_searches.findIndex((elem) => elem === query)
      if (index >= 0) {
        state.recent_searches.splice(index, 1)
      }

      state.recent_searches.splice(0, 0, query)

      if (state.recent_searches.length > 5) {
        state.recent_searches.pop()
      }
    },
    [types.HIDE_SINGLES](state, hideSingles) {
      state.hide_singles = hideSingles
    },
    [types.HIDE_SPOTIFY](state, hideSpotify) {
      state.hide_spotify = hideSpotify
    },
    [types.ARTISTS_SORT](state, sort) {
      state.artists_sort = sort
    },
    [types.ARTIST_ALBUMS_SORT](state, sort) {
      state.artist_albums_sort = sort
    },
    [types.ALBUMS_SORT](state, sort) {
      state.albums_sort = sort
    },
    [types.SHOW_ONLY_NEXT_ITEMS](state, showOnlyNextItems) {
      state.show_only_next_items = showOnlyNextItems
    },
    [types.SHOW_BURGER_MENU](state, showBurgerMenu) {
      state.show_burger_menu = showBurgerMenu
    },
    [types.SHOW_PLAYER_MENU](state, showPlayerMenu) {
      state.show_player_menu = showPlayerMenu
    },
    [types.SHOW_UPDATE_DIALOG](state, showUpdateDialog) {
      state.show_update_dialog = showUpdateDialog
    },
    [types.UPDATE_DIALOG_SCAN_KIND](state, scanKind) {
      state.update_dialog_scan_kind = scanKind
    }
  },

  actions: {
    add_notification({ commit, state }, notification) {
      const newNotification = {
        id: state.notifications.next_id++,
        type: notification.type,
        text: notification.text,
        topic: notification.topic,
        timeout: notification.timeout
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
