import { createStore } from 'vuex'
import * as types from './mutation_types'

export default createStore({
  state() {
    return {
      albums_sort: 1,
      artist_albums_sort: 1,
      artist_tracks_sort: 1,
      artists_sort: 1,
      composer_tracks_sort: 1,
      config: {
        buildoptions: [],
        version: '',
        websocket_port: 0
      },
      genre_tracks_sort: 1,
      hide_singles: false,
      hide_spotify: false,
      lastfm: {},
      library: {
        albums: 0,
        artists: 0,
        db_playtime: 0,
        songs: 0,
        started_at: '01',
        updated_at: '01',
        updating: false
      },
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
        repeat: 'off',
        shuffle: false,
        state: 'stop',
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
    now_playing: (state) =>
      state.queue.items.find((e) => e.id === state.player.item_id) ?? {},
    settings_option: (state) => (categoryName, optionName) =>
      state.settings.categories
        .find((category) => category.name === categoryName)
        ?.options.find((option) => option.name === optionName) ?? {},
    settings_option_recently_added_limit: (state, getters) =>
      getters.settings_webinterface?.options.find(
        (option) => option.name === 'recently_added_limit'
      )?.value ?? 100,
    settings_option_show_composer_for_genre: (state, getters) =>
      getters.settings_webinterface?.options.find(
        (option) => option.name === 'show_composer_for_genre'
      )?.value ?? null,
    settings_option_show_composer_now_playing: (state, getters) =>
      getters.settings_webinterface?.options.find(
        (option) => option.name === 'show_composer_now_playing'
      )?.value ?? false,
    settings_option_show_filepath_now_playing: (state, getters) =>
      getters.settings_webinterface?.options.find(
        (option) => option.name === 'show_filepath_now_playing'
      )?.value ?? false,
    settings_webinterface: (state) =>
      state.settings?.categories.find(
        (category) => category.name === 'webinterface'
      ) ?? null
  },

  mutations: {
    [types.UPDATE_CONFIG](state, config) {
      state.config = config
    },
    [types.UPDATE_SETTINGS](state, settings) {
      state.settings = settings
    },
    [types.UPDATE_LIBRARY_STATS](state, libraryStats) {
      state.library = libraryStats
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
    [types.SEARCH_SOURCE](state, searchSource) {
      state.search_source = searchSource
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
        text: notification.text,
        timeout: notification.timeout,
        topic: notification.topic,
        type: notification.type
      }
      if (newNotification.topic) {
        const index = state.notifications.list.findIndex(
          (elem) => elem.topic === newNotification.topic
        )
        if (index >= 0) {
          state.notifications.list.splice(index, 1, newNotification)
          return
        }
      }
      state.notifications.list.push(newNotification)
      if (notification.timeout > 0) {
        setTimeout(() => {
          this.dispatch('delete_notification', newNotification)
        }, notification.timeout)
      }
    },
    add_recent_search({ commit, state }, query) {
      const index = state.recent_searches.findIndex((elem) => elem === query)
      if (index >= 0) {
        state.recent_searches.splice(index, 1)
      }
      state.recent_searches.splice(0, 0, query)
      if (state.recent_searches.length > 5) {
        state.recent_searches.pop()
      }
    },
    delete_notification({ commit, state }, notification) {
      const index = state.notifications.list.indexOf(notification)
      if (index !== -1) {
        state.notifications.list.splice(index, 1)
      }
    },
    update_settings_option({ commit, state }, option) {
      const settingCategory = state.settings.categories.find(
          (e) => e.name === option.category
        ),
        settingOption = settingCategory.options.find(
          (e) => e.name === option.name
        )
      settingOption.value = option.value
    }
  }
})
