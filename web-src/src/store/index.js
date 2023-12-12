import { createStore } from 'vuex'
import * as types from './mutation_types'

export default createStore({
  state() {
    return {
      albums_sort: 1,
      artists_sort: 1,
      artist_albums_sort: 1,
      artist_tracks_sort: 1,
      audiobooks_count: {},
      composer_tracks_sort: 1,
      config: {
        buildoptions: [],
        version: '',
        websocket_port: 0
      },
      genre_tracks_sort: 1,
      hide_singles: false,
      hide_spotify: false,
      library: {
        albums: 0,
        artists: 0,
        db_playtime: 0,
        songs: 0,
        started_at: '01',
        updated_at: '01',
        updating: false
      },
      podcasts_count: {},
      lastfm: {},
      lyrics: {
        content: [],
        pane: false
      },
      notifications: {
        list: [],
        next_id: 1
      },
      outputs: [],
      pairing: {},
      player: {
        consume: false,
        item_id: 0,
        item_length_ms: 0,
        item_progress_ms: 0,
        shuffle: false,
        state: 'stop',
        repeat: 'off',
        volume: 0
      },
      queue: {
        count: 0,
        items: [],
        version: 0
      },
      recent_searches: [],
      rss_count: {},
      search_source: 'library',
      settings: {
        categories: []
      },
      show_only_next_items: false,
      show_burger_menu: false,
      show_player_menu: false,
      show_update_dialog: false,
      spotify: {},
      spotify_featured_playlists: [],
      spotify_new_releases: [],
      update_dialog_scan_kind: ''
    }
  },

  getters: {
    lyrics: (state) => state.lyrics.content,

    lyrics_pane: (state) => state.lyrics.pane,

    now_playing: (state) => {
      const item = state.queue.items.find((e) => e.id === state.player.item_id)
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

    settings_option_show_filepath_now_playing: (state, getters) => {
      if (getters.settings_webinterface) {
        const option = getters.settings_webinterface.options.find(
          (elem) => elem.name === 'show_filepath_now_playing'
        )
        if (option) {
          return option.value
        }
      }
      return false
    },

    settings_category: (state) => (categoryName) =>
      state.settings.categories.find((e) => e.name === categoryName),

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
          (e) => e.name === option.category
        ),
        settingOption = settingCategory.options.find(
          (e) => e.name === option.name
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
    [types.UPDATE_LYRICS](state, lyrics) {
      state.lyrics.content = lyrics
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
    [types.SEARCH_SOURCE](state, searchSource) {
      state.search_source = searchSource
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
    [types.COMPOSER_TRACKS_SORT](state, sort) {
      state.composer_tracks_sort = sort
    },
    [types.GENRE_TRACKS_SORT](state, sort) {
      state.genre_tracks_sort = sort
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
    [types.ARTIST_TRACKS_SORT](state, sort) {
      state.artist_tracks_sort = sort
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
